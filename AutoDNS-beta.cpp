// AutoDNS.xex 1.0.4
//
// Blind Boot + State Machine + Runtime Diagnostics
//
// Filosofia:
//   1. DNS persistente permanece apontando para 192.0.2.1.
//   2. Dashboard inicia offline.
//   3. Após o plugin carregar, as configurações são carregadas em RAM.
//   4. SYSAPP recebe DNS válido.
//   5. SYSAPP é verificado separadamente.
//   6. Aguardamos a proteção/runtime hooks ficarem disponíveis.
//   7. TITLE recebe DNS válido.
//   8. TITLE é verificado separadamente.
//   9. Nenhuma configuração é gravada no armazenamento.
//
// IMPORTANTE:
//   XnpConfig() altera o estado runtime. A configuração persistente
//   permanece com o DNS falso.
//
// AutoDNS 1.0.4
//

#include <xtl.h>
#include <stddef.h>
#include <string.h>

//
// ============================================================================
// CONFIGURAÇÃO
// ============================================================================
//

// DNS persistente "morto":
// 192.0.2.1 = TEST-NET-1 / RFC 5737
#define DEAD_DNS       0xC0000201u

// DNS runtime
#define GOOD_DNS1      0x01010101u     // 1.1.1.1
#define GOOD_DNS2      0x01000001u     // 1.0.0.1

//
// Tempo máximo para obter endereço IP.
//
#define ADDRESS_WAIT_MS        90000

//
// Tempo máximo para cada tentativa de XnpConfig.
//
#define CONFIG_RETRY_MS        30000

//
// Intervalo entre tentativas.
//
#define RETRY_INTERVAL_MS      1000

//
// Quantidade máxima de tentativas.
//
#define CONFIG_MAX_RETRIES     30

//
// Após SYSAPP estar operacional, damos uma pequena janela para que
// plugins subsequentes instalem seus hooks.
//
// Diferentemente da versão 1.0.3, esse tempo NÃO é a condição de sucesso.
// É apenas uma janela adicional de segurança.
//
#define PROTECTION_GRACE_MS    10000

//
// Intervalo de logging.
//
#define LOG_PREFIX "[AutoDNS 1.0.4] "

//
// ============================================================================
// TIPOS / EXPORTS
// ============================================================================
//

typedef LONG NTSTATUS;

extern "C"
{
    NTSTATUS ExCreateThread(
        PHANDLE,
        DWORD,
        LPDWORD,
        PVOID,
        LPTHREAD_START_ROUTINE,
        LPVOID,
        DWORD
    );

    NTSTATUS XexGetModuleHandle(
        PCHAR,
        PHANDLE
    );

    NTSTATUS XexGetProcedureAddress(
        HANDLE,
        DWORD,
        PVOID *
    );
}

//
// Contextos XAM
//
#define SYSAPP 2
#define TITLE  1

#pragma pack(push, 1)

typedef struct
{
    DWORD ina;
    DWORD inaOnline;
    WORD  port;
    BYTE  enet[6];
    BYTE  online[20];

} XNADDR_;

//
// XNetConfigParams
// Layout conhecido do xkelib.
//
typedef struct
{
    BYTE hash[0x14];
    BYTE confounder[8];

    WORD name[0x18];
    WORD flags;

    BYTE enet[6];

    DWORD ina;
    DWORD mask;
    DWORD gw;

    DWORD dns[2];

    char host[0x28];

    char pppoe[
        0x40 +
        0x40 +
        0x28 +
        0x28
    ];

    LARGE_INTEGER leaseTime;

    DWORD leaseSecs;

    DWORD rest[
        3 +
        4 +
        4
    ];

    BYTE tail[
        0x44 +
        16
    ];

} CFG;

#pragma pack(pop)

//
// Segurança estrutural
//
C_ASSERT(sizeof(CFG) == 492);
C_ASSERT(offsetof(CFG, flags) == 0x4C);
C_ASSERT(offsetof(CFG, dns) == 0x60);
C_ASSERT(offsetof(CFG, leaseSecs) == 0x168);

//
// XAM exports
//
//  51  NetDll_XNetStartup
//  73  NetDll_XNetGetTitleXnAddr
// 101  NetDll_XnpLoadConfigParams
// 104  NetDll_XnpConfig
//

static int
(*pXNetStartup)(int, BYTE *);

static DWORD
(*pXNetGetTitleXnAddr)(int, XNADDR_ *);

static int
(*pXnpConfig)(int, CFG *, DWORD);

static int
(*pXnpLoadConfigParams)(int, CFG *, DWORD, DWORD);

//
// ============================================================================
// LOGGING
// ============================================================================
//

static void Log(const char *msg)
{
    //
    // OutputDebugStringA está disponível no ambiente Xbox 360/XDK.
    //
    OutputDebugStringA(LOG_PREFIX);
    OutputDebugStringA(msg);
    OutputDebugStringA("\r\n");
}

static void LogState(const char *state)
{
    OutputDebugStringA(LOG_PREFIX);
    OutputDebugStringA("STATE -> ");
    OutputDebugStringA(state);
    OutputDebugStringA("\r\n");
}

static void LogConfig(const char *context, const CFG *c)
{
    char buffer[256];

    wsprintfA(
        buffer,
        "%s CFG[%s]: IP=%08X MASK=%08X GW=%08X DNS1=%08X DNS2=%08X lease=%u flags=%04X",
        LOG_PREFIX,
        context,
        c->ina,
        c->mask,
        c->gw,
        c->dns[0],
        c->dns[1],
        c->leaseSecs,
        c->flags
    );

    OutputDebugStringA(buffer);
}

static void LogResult(
    const char *operation,
    int result
)
{
    char buffer[128];

    wsprintfA(
        buffer,
        "%s %s returned %d (0x%08X)",
        LOG_PREFIX,
        operation,
        result,
        result
    );

    OutputDebugStringA(buffer);
}

//
// ============================================================================
// STATE MACHINE
// ============================================================================
//

typedef enum
{
    STATE_INIT = 0,

    STATE_WAIT_NETWORK,

    STATE_LOAD_CONFIG,

    STATE_VALIDATE_CONFIG,

    STATE_CONFIG_SYSAPP,

    STATE_VERIFY_SYSAPP,

    STATE_PROTECTION_WAIT,

    STATE_CONFIG_TITLE,

    STATE_VERIFY_TITLE,

    STATE_ONLINE,

    STATE_FAILED

} AUTODNS_STATE;

static AUTODNS_STATE g_state = STATE_INIT;

static void SetState(AUTODNS_STATE state)
{
    g_state = state;

    switch (state)
    {
        case STATE_INIT:
            LogState("INIT");
            break;

        case STATE_WAIT_NETWORK:
            LogState("WAIT_NETWORK");
            break;

        case STATE_LOAD_CONFIG:
            LogState("LOAD_CONFIG");
            break;

        case STATE_VALIDATE_CONFIG:
            LogState("VALIDATE_CONFIG");
            break;

        case STATE_CONFIG_SYSAPP:
            LogState("CONFIG_SYSAPP");
            break;

        case STATE_VERIFY_SYSAPP:
            LogState("VERIFY_SYSAPP");
            break;

        case STATE_PROTECTION_WAIT:
            LogState("PROTECTION_WAIT");
            break;

        case STATE_CONFIG_TITLE:
            LogState("CONFIG_TITLE");
            break;

        case STATE_VERIFY_TITLE:
            LogState("VERIFY_TITLE");
            break;

        case STATE_ONLINE:
            LogState("ONLINE");
            break;

        case STATE_FAILED:
            LogState("FAILED");
            break;

        default:
            LogState("UNKNOWN");
            break;
    }
}

//
// ============================================================================
// RESOLVE XAM
// ============================================================================
//

static BOOL Resolve()
{
    HANDLE xam = NULL;

    Log("Resolving xam.xex...");

    if (XexGetModuleHandle("xam.xex", &xam) < 0)
    {
        Log("ERROR: XexGetModuleHandle(xam.xex) failed.");
        return FALSE;
    }

    struct
    {
        DWORD ord;
        PVOID *fn;

    } table[] =
    {
        {  51, (PVOID *)&pXNetStartup },
        {  73, (PVOID *)&pXNetGetTitleXnAddr },
        { 101, (PVOID *)&pXnpLoadConfigParams },
        { 104, (PVOID *)&pXnpConfig }
    };

    for (
        size_t i = 0;
        i < sizeof(table) / sizeof(table[0]);
        i++
    )
    {
        if (
            XexGetProcedureAddress(
                xam,
                table[i].ord,
                table[i].fn
            ) < 0
        )
        {
            Log("ERROR: Failed resolving XAM export.");
            return FALSE;
        }

        if (*table[i].fn == NULL)
        {
            Log("ERROR: XAM export resolved to NULL.");
            return FALSE;
        }
    }

    Log("XAM exports resolved successfully.");

    return TRUE;
}

//
// ============================================================================
// NETWORK STATE
// ============================================================================
//

static BOOL GetNetworkAddress(
    int caller,
    XNADDR_ *addr
)
{
    memset(
        addr,
        0,
        sizeof(XNADDR_)
    );

    DWORD flags =
        pXNetGetTitleXnAddr(
            caller,
            addr
        );

    BOOL configured =
        (flags & 0xC) != 0;

    BOOL none =
        (flags & 0x1) != 0;

    if (
        configured &&
        !none &&
        addr->ina != 0
    )
    {
        return TRUE;
    }

    return FALSE;
}

//
// ============================================================================
// WAIT NETWORK
// ============================================================================
//

static BOOL WaitForAddress(
    int caller,
    DWORD timeoutMs
)
{
    DWORD start =
        GetTickCount();

    char context[64];

    if (caller == SYSAPP)
        lstrcpyA(context, "SYSAPP");

    else
        lstrcpyA(context, "TITLE");

    Log("Waiting for network address...");

    for (;;)
    {
        XNADDR_ addr;

        if (
            GetNetworkAddress(
                caller,
                &addr
            )
        )
        {
            char buffer[128];

            wsprintfA(
                buffer,
                "%s network address ready: IP=%08X",
                context,
                addr.ina
            );

            OutputDebugStringA(buffer);
            OutputDebugStringA("\r\n");

            return TRUE;
        }

        if (
            GetTickCount() - start >
            timeoutMs
        )
        {
            Log("Network address timeout.");

            return FALSE;
        }

        Sleep(500);
    }
}

//
// ============================================================================
// LOAD CONFIG
// ============================================================================
//

static BOOL Load(CFG *c)
{
    memset(
        c,
        0,
        sizeof(CFG)
    );

    int result =
        pXnpLoadConfigParams(
            SYSAPP,
            c,
            0,
            0
        );

    LogResult(
        "XnpLoadConfigParams",
        result
    );

    //
    // Heurística de validação usada na versão anterior.
    //
    if (
        c->leaseSecs >
        30u * 24u * 3600u
    )
    {
        Log(
            "ERROR: Configuration validation failed: leaseSecs."
        );

        return FALSE;
    }

    if (
        c->flags >= 0x1000
    )
    {
        Log(
            "ERROR: Configuration validation failed: flags."
        );

        return FALSE;
    }

    LogConfig(
        "LOADED",
        c
    );

    return TRUE;
}

//
// ============================================================================
// DNS CHECK
// ============================================================================
//

static BOOL IsDeadDNS(
    DWORD dns
)
{
    return dns == DEAD_DNS;
}

static BOOL HasDeadDNS(
    const CFG *c
)
{
    if (
        IsDeadDNS(c->dns[0]) ||
        IsDeadDNS(c->dns[1])
    )
    {
        return TRUE;
    }

    return FALSE;
}

//
// ============================================================================
// CONFIGURE CONTEXT
// ============================================================================
//
// Faz várias tentativas.
//
// IMPORTANTE:
// XnpConfig retorna imediatamente enquanto a configuração pode continuar
// sendo processada internamente.
//
// Portanto, sucesso de XnpConfig NÃO significa sozinho que o contexto
// já está operacional.
//
static BOOL ConfigureContext(
    int caller,
    CFG *cfg
)
{
    char context[32];

    if (caller == SYSAPP)
        lstrcpyA(context, "SYSAPP");
    else
        lstrcpyA(context, "TITLE");

    Log("Applying runtime DNS configuration...");
    LogConfig(context, cfg);

    for (
        DWORD attempt = 1;
        attempt <= CONFIG_MAX_RETRIES;
        attempt++
    )
    {
        int result =
            pXnpConfig(
                caller,
                cfg,
                0
            );

        char buffer[128];

        wsprintfA(
            buffer,
            "%s XnpConfig(%s) attempt %u/%u -> %d (0x%08X)",
            LOG_PREFIX,
            context,
            attempt,
            CONFIG_MAX_RETRIES,
            result,
            result
        );

        OutputDebugStringA(buffer);
        OutputDebugStringA("\r\n");

        //
        // Assumimos sucesso quando XnpConfig aceita a chamada.
        //
        // A confirmação efetiva acontece em VerifyContext().
        //
        if (result == 0)
        {
            Log("XnpConfig accepted configuration.");

            return TRUE;
        }

        Sleep(RETRY_INTERVAL_MS);
    }

    Log("ERROR: XnpConfig failed after maximum retries.");

    return FALSE;
}

//
// ============================================================================
// VERIFY CONTEXT
// ============================================================================
//
// Aqui há uma limitação importante:
// XnpLoadConfigParams() lê os parâmetros de configuração disponíveis
// através do XAM e não deve ser interpretado automaticamente como uma
// leitura direta do resolver interno.
//
// Ainda assim, verificamos o estado de rede do caller separadamente.
//
// A função também aguarda o contexto obter endereço.
//
// ============================================================================
//

static BOOL VerifyContext(
    int caller,
    DWORD timeoutMs
)
{
    char context[32];

    if (caller == SYSAPP)
        lstrcpyA(context, "SYSAPP");
    else
        lstrcpyA(context, "TITLE");

    Log("Verifying network context...");

    if (
        !WaitForAddress(
            caller,
            timeoutMs
        )
    )
    {
        Log("ERROR: Context did not obtain valid address.");

        return FALSE;
    }

    Log("Context network state is operational.");

    return TRUE;
}

//
// ============================================================================
// APPLY DNS TO RUNTIME CFG
// ============================================================================
//

static BOOL PrepareRuntimeConfig(
    CFG *cfg
)
{
    Log("Preparing runtime DNS configuration...");

    if (
        !HasDeadDNS(cfg)
    )
    {
        Log(
            "Stored DNS is already valid. Blind Boot configuration not required."
        );

        return FALSE;
    }

    //
    // Alteração SOMENTE da cópia em RAM.
    //
    cfg->dns[0] = GOOD_DNS1;
    cfg->dns[1] = GOOD_DNS2;

    LogConfig(
        "RUNTIME",
        cfg
    );

    return TRUE;
}

//
// ============================================================================
// MAIN STATE MACHINE
// ============================================================================
//

static void Run()
{
    //
    // A CFG precisa permanecer viva durante todas as chamadas.
    //
    static CFG cfg;

    SetState(
        STATE_WAIT_NETWORK
    );

    //
    // ------------------------------------------------------------
    // STATE: WAIT_NETWORK
    // ------------------------------------------------------------
    //

    if (
        !WaitForAddress(
            SYSAPP,
            ADDRESS_WAIT_MS
        )
    )
    {
        Log(
            "ERROR: SYSAPP network never became ready."
        );

        SetState(
            STATE_FAILED
        );

        return;
    }

    //
    // ------------------------------------------------------------
    // STATE: LOAD_CONFIG
    // ------------------------------------------------------------
    //

    SetState(
        STATE_LOAD_CONFIG
    );

    if (
        !Load(&cfg)
    )
    {
        SetState(
            STATE_FAILED
        );

        return;
    }

    //
    // ------------------------------------------------------------
    // STATE: VALIDATE_CONFIG
    // ------------------------------------------------------------
    //

    SetState(
        STATE_VALIDATE_CONFIG
    );

    if (
        !PrepareRuntimeConfig(&cfg)
    )
    {
        //
        // Se o DNS não é o DNS morto, não mexemos.
        //
        Log(
            "Nothing to change. Exiting without modifying network configuration."
        );

        SetState(
            STATE_ONLINE
        );

        return;
    }

    //
    // ------------------------------------------------------------
    // STATE: CONFIG_SYSAPP
    // ------------------------------------------------------------
    //

    SetState(
        STATE_CONFIG_SYSAPP
    );

    if (
        !ConfigureContext(
            SYSAPP,
            &cfg
        )
    )
    {
        SetState(
            STATE_FAILED
        );

        return;
    }

    //
    // ------------------------------------------------------------
    // STATE: VERIFY_SYSAPP
    // ------------------------------------------------------------
    //

    SetState(
        STATE_VERIFY_SYSAPP
    );

    if (
        !VerifyContext(
            SYSAPP,
            CONFIG_RETRY_MS
        )
    )
    {
        Log(
            "ERROR: SYSAPP configuration could not be verified."
        );

        SetState(
            STATE_FAILED
        );

        return;
    }

    Log(
        "SYSAPP runtime configuration is operational."
    );

    //
    // ------------------------------------------------------------
    // STATE: PROTECTION_WAIT
    // ------------------------------------------------------------
    //
    // Não tratamos esse delay como "prova" de que xbGuard está ativo.
    //
    // É somente uma janela para permitir que plugins posteriores carreguem.
    //
    //

    SetState(
        STATE_PROTECTION_WAIT
    );

    Log(
        "Waiting for subsequent protection plugins..."
    );

    Sleep(
        PROTECTION_GRACE_MS
    );

    //
    // ------------------------------------------------------------
    // STATE: CONFIG_TITLE
    // ------------------------------------------------------------
    //

    SetState(
        STATE_CONFIG_TITLE
    );

    if (
        !ConfigureContext(
            TITLE,
            &cfg
        )
    )
    {
        Log(
            "ERROR: TITLE XnpConfig failed."
        );

        SetState(
            STATE_FAILED
        );

        return;
    }

    //
    // ------------------------------------------------------------
    // STATE: VERIFY_TITLE
    // ------------------------------------------------------------
    //

    SetState(
        STATE_VERIFY_TITLE
    );

    if (
        !VerifyContext(
            TITLE,
            CONFIG_RETRY_MS
        )
    )
    {
        Log(
            "ERROR: TITLE configuration could not be verified."
        );

        SetState(
            STATE_FAILED
        );

        return;
    }

    //
    // ------------------------------------------------------------
    // STATE: ONLINE
    // ------------------------------------------------------------
    //

    Log(
        "TITLE runtime network is operational."
    );

    Log(
        "Persistent configuration remains untouched."
    );

    Log(
        "Blind Boot protection completed successfully."
    );

    SetState(
        STATE_ONLINE
    );
}

//
// ============================================================================
// WORKER
// ============================================================================
//

static DWORD WINAPI Worker(
    LPVOID
)
{
    SetState(
        STATE_INIT
    );

    Log(
        "AutoDNS 1.0.4 starting."
    );

    //
    // Resolve XAM
    //
    if (
        !Resolve()
    )
    {
        SetState(
            STATE_FAILED
        );

        return 0;
    }

    //
    // Start XNet SYSAPP context
    //
    BYTE startup[13] =
    {
        13
    };

    Log(
        "Starting XNet SYSAPP context..."
    );

    int result =
        pXNetStartup(
            SYSAPP,
            startup
        );

    LogResult(
        "XNetStartup(SYSAPP)",
        result
    );

    if (
        result != 0
    )
    {
        Log(
            "ERROR: XNetStartup failed."
        );

        SetState(
            STATE_FAILED
        );

        return 0;
    }

    //
    // Execute state machine.
    //
    Run();

    //
    // Intentionally no XNetCleanup().
    //
    // The plugin remains resident for console uptime.
    //
    Log(
        "Worker finished; AutoDNS remains resident."
    );

    return 0;
}

//
// ============================================================================
// DLL ENTRY
// ============================================================================
//

extern "C"
BOOL WINAPI DllMain(
    HANDLE,
    DWORD reason,
    LPVOID
)
{
    if (
        reason != DLL_PROCESS_ATTACH
    )
    {
        return TRUE;
    }

    HANDLE h = NULL;

    NTSTATUS status =
        ExCreateThread(
            &h,
            0,
            NULL,
            NULL,
            Worker,
            NULL,
            2
        );

    if (
        status >= 0 &&
        h != NULL
    )
    {
        CloseHandle(h);
    }

    return TRUE;
}
