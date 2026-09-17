// AutoDNS-beta6.xex, a DashLaunch sysdll plugin.
//
// Applies working DNS servers to both SYSAPP and TITLE contexts after boot,
// replacing the intentionally dead 192.0.2.1 stored in NAND.
// Storage is never touched; next boot starts offline again.
//
// Setup: Network Settings -> DNS Manual -> 192.0.2.1 for both servers.

#include <xtl.h>
#include <stddef.h>
#include <string.h>

// --- DNS configuration ---------------------------------------------------
#define DEAD_DNS         0xC0000201u  // 192.0.2.1 (RFC 5737, never routed)
#ifndef GOOD_DNS1
#define GOOD_DNS1        0x01010101u  // 1.1.1.1 (Cloudflare default)
#endif
#ifndef GOOD_DNS2
#define GOOD_DNS2        0x01000001u  // 1.0.0.1 (Cloudflare default)
#endif

// --- Timing --------------------------------------------------------------
#define BOOT_WAIT          90000      // ms: wait for DHCP/Wi-Fi at boot
#define SWAP_WAIT          30000      // ms: wait for stack after XnpConfig
#define TITLE_DELAY        10000      // ms: safety margin before TITLE context
#define APPLY_RETRIES      3          // XnpConfig attempts per context
#define APPLY_RETRY_WAIT   5000       // ms between retries

// --- Network caller contexts ---------------------------------------------
#define SYSAPP 2                      // dashboard and system
#define TITLE  1                      // games and apps

// --- XNetGetTitleXnAddr status flags -------------------------------------
#define XADDR_NONE       0x01
#define XADDR_ETHERNET   0x02
#define XADDR_STATIC     0x04
#define XADDR_DHCP       0x08
#define XADDR_PPPOE      0x10
#define XADDR_GATEWAY    0x20
#define XADDR_DNS        0x40
#define XADDR_ONLINE     0x80

typedef LONG NTSTATUS;

extern "C" {
    NTSTATUS ExCreateThread(PHANDLE, DWORD, LPDWORD, PVOID,
                            LPTHREAD_START_ROUTINE, LPVOID, DWORD);
    NTSTATUS XexGetModuleHandle(PCHAR, PHANDLE);
    NTSTATUS XexGetProcedureAddress(HANDLE, DWORD, PVOID *);
}

#pragma pack(push, 1)
typedef struct {
    DWORD ina, inaOnline;
    WORD  port;
    BYTE  enet[6], online[20];
} XNADDR_;

// XNetConfigParams — 492 bytes, layout from xkelib.
typedef struct {
    BYTE  hash[0x14], confounder[8];
    WORD  name[0x18], flags;
    BYTE  enet[6];
    DWORD ina, mask, gw, dns[2];
    char  host[0x28], pppoe[0x40 + 0x40 + 0x28 + 0x28];
    LARGE_INTEGER leaseTime;
    DWORD leaseSecs, rest[3 + 4 + 4];
    BYTE  tail[0x44 + 16];
} CFG;
#pragma pack(pop)

// Security assertions to prevent layout regressions.
C_ASSERT(sizeof(CFG)              == 492);
C_ASSERT(offsetof(CFG, flags)     == 0x4C);
C_ASSERT(offsetof(CFG, dns)       == 0x60);
C_ASSERT(offsetof(CFG, leaseSecs) == 0x168);

// xam.xex exports by ordinal.
static int   (*pXNetStartup)(int, BYTE *);
static DWORD (*pXNetGetTitleXnAddr)(int, XNADDR_ *);
static int   (*pXnpConfig)(int, CFG *, DWORD);
static int   (*pXnpLoadConfigParams)(int, CFG *, DWORD, DWORD);

// Separate structs per context to avoid race conditions during async write-back.
static CFG  g_cfg_sys;
static CFG  g_cfg_title;

// XNetStartupParams: first byte is size, zeros mean defaults.
static BYTE g_startup[13] = { 13 };

// -------------------------------------------------------------------------
// Resolve all required xam.xex exports by ordinal.
// Returns FALSE if any export is missing.
// -------------------------------------------------------------------------
static BOOL Resolve()
{
    HANDLE xam;
    if (XexGetModuleHandle("xam.xex", &xam) < 0)
        return FALSE;

    struct { DWORD ord; PVOID *fn; } table[] = {
        {  51, (PVOID *)&pXNetStartup         },
        {  73, (PVOID *)&pXNetGetTitleXnAddr  },
        { 101, (PVOID *)&pXnpLoadConfigParams },
        { 104, (PVOID *)&pXnpConfig           },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (XexGetProcedureAddress(xam, table[i].ord, table[i].fn) < 0 ||
            *table[i].fn == NULL)
            return FALSE;
    }
    return TRUE;
}

// -------------------------------------------------------------------------
// Polls XNetGetTitleXnAddr until valid IP + DNS configured or timeout.
// -------------------------------------------------------------------------
static BOOL WaitForAddress(DWORD ms, int caller)
{
    DWORD t0 = GetTickCount();

    for (;;) {
        XNADDR_ a;
        memset(&a, 0, sizeof(a));
        DWORD flags = pXNetGetTitleXnAddr(caller, &a);

        BOOL hasIp  = (a.ina != 0);
        BOOL hasDns = (flags & XADDR_DNS)  != 0;
        BOOL hasNet = (flags & (XADDR_STATIC | XADDR_DHCP)) != 0;
        BOOL none   = (flags & XADDR_NONE) != 0;

        if (hasNet && hasDns && hasIp && !none)
            return TRUE;

        if (GetTickCount() - t0 > ms)
            return FALSE;
        Sleep(500);
    }
}

// -------------------------------------------------------------------------
// Reads and decrypts stored network config. Validates against garbage data.
// -------------------------------------------------------------------------
static BOOL Load(CFG *dst)
{
    memset(dst, 0, sizeof(*dst));
    pXnpLoadConfigParams(SYSAPP, dst, 0, 0);
    return dst->leaseSecs <= 30u * 24 * 3600 && dst->flags < 0x1000;
}

// -------------------------------------------------------------------------
// Initializes XNet context for given caller. Must succeed before XnpConfig.
// -------------------------------------------------------------------------
static BOOL StartContext(int caller)
{
    int rc = pXNetStartup(caller, g_startup);
    if (rc != 0)
        return FALSE;
    return TRUE;
}

// -------------------------------------------------------------------------
// Applies DNS config with retry and stack readiness verification.
// Substitutes both slots if at least one holds DEAD_DNS.
// -------------------------------------------------------------------------
static BOOL ApplyDNS(int caller, CFG *cfg)
{
    for (int attempt = 1; attempt <= APPLY_RETRIES; attempt++) {
        int rc = pXnpConfig(caller, cfg, 0);
        if (rc != 0) {
            Sleep(APPLY_RETRY_WAIT);
            continue;
        }
        if (WaitForAddress(SWAP_WAIT, caller))
            return TRUE;
        Sleep(APPLY_RETRY_WAIT);
    }
    return FALSE;
}

// -------------------------------------------------------------------------
// Main initialization logic. Two-phase injection with separate contexts.
// No background watchdog thread.
// -------------------------------------------------------------------------
static void Run()
{
    if (!WaitForAddress(BOOT_WAIT, SYSAPP))
        return;
    if (!Load(&g_cfg_sys))
        return;

    // Only act if at least one slot still holds the dead address.
    if (g_cfg_sys.dns[0] != DEAD_DNS && g_cfg_sys.dns[1] != DEAD_DNS)
        return;

    g_cfg_sys.dns[0] = GOOD_DNS1;
    g_cfg_sys.dns[1] = GOOD_DNS2;

    // Phase 1: SYSAPP (Dashboard)
    if (!ApplyDNS(SYSAPP, &g_cfg_sys))
        return;

#if TITLE_DELAY > 0
    Sleep(TITLE_DELAY);
#endif

    // Phase 2: TITLE (Games/Apps) with separate struct copy.
    memcpy(&g_cfg_title, &g_cfg_sys, sizeof(CFG));

    if (!StartContext(TITLE))
        return;
    if (!ApplyDNS(TITLE, &g_cfg_title))
        return;
}

// -------------------------------------------------------------------------
static DWORD WINAPI Worker(LPVOID)
{
    if (!Resolve())
        return 0;
    if (!StartContext(SYSAPP))
        return 0;
    Run();
    return 0;
}

extern "C" BOOL WINAPI DllMain(HANDLE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    HANDLE h = NULL;
    if (ExCreateThread(&h, 0, NULL, NULL, Worker, NULL, 2) >= 0 && h != NULL)
        CloseHandle(h);
    return TRUE;
}
