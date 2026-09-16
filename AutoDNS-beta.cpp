// AutoDNS-beta.xex, a DashLaunch sysdll plugin.
//
// Two-phase DNS injection for environments using the stock dashboard 
// alongside local stealth services (e.g., Proton).
//
// Phase 1: Applies valid DNS to SYSAPP (Dashboard) for authentication.
// Phase 2: Waits for stealth plugins to initialize hooks.
// Phase 3: Applies valid DNS to TITLE (Games/Apps) to ensure full connectivity.

#include <xtl.h>
#include <stddef.h>
#include <string.h>

// DNS servers to switch to. Overridden by build.sh via -D flags.
#ifndef GOOD_DNS1
#define GOOD_DNS1  0x01010101u   // 1.1.1.1
#endif
#ifndef GOOD_DNS2
#define GOOD_DNS2  0x01000001u   // 1.0.0.1
#endif

#define BOOT_WAIT    90000   // ms to wait for Wi-Fi/DHCP at boot
#define SWAP_WAIT    30000   // ms to wait for DHCP after final XnpConfig
#define TITLE_DELAY  10000   // ms to wait for stealth plugins to hook xam

#define SYSAPP 2   // XNCALLER_SYSAPP
#define TITLE  1   // XNCALLER_TITLE

typedef LONG NTSTATUS;
extern "C" {
    NTSTATUS ExCreateThread(PHANDLE, DWORD, LPDWORD, PVOID, LPTHREAD_START_ROUTINE, LPVOID, DWORD);
    NTSTATUS XexGetModuleHandle(PCHAR, PHANDLE);
    NTSTATUS XexGetProcedureAddress(HANDLE, DWORD, PVOID *);
}

#pragma pack(push, 1)
typedef struct {
    DWORD ina, inaOnline;
    WORD  port;
    BYTE  enet[6], online[20];
} XNADDR_;

// XNetConfigParams. 492 bytes, layout from xkelib.
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

// Security assertions from v1.1.0 to prevent layout regressions.
C_ASSERT(sizeof(CFG) == 492);
C_ASSERT(offsetof(CFG, flags) == 0x4C);
C_ASSERT(offsetof(CFG, dns) == 0x60);
C_ASSERT(offsetof(CFG, leaseSecs) == 0x168);

// xam.xex exports by ordinal.
static int   (*pXNetStartup)(int, BYTE *);
static DWORD (*pXNetGetTitleXnAddr)(int, XNADDR_ *);
static int   (*pXnpConfig)(int, CFG *, DWORD);
static int   (*pXnpLoadConfigParams)(int, CFG *, DWORD, DWORD);

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
        if (XexGetProcedureAddress(xam, table[i].ord, table[i].fn) < 0 || *table[i].fn == NULL)
            return FALSE;
    }
    return TRUE;
}

// Polls XNetGetTitleXnAddr until the console has an address for the given caller.
static BOOL WaitForAddress(int caller, DWORD ms)
{
    DWORD t0 = GetTickCount();
    for (;;) {
        XNADDR_ a;
        memset(&a, 0, sizeof(a));
        DWORD flags = pXNetGetTitleXnAddr(caller, &a);
        BOOL configured = (flags & 0xC) != 0;   // STATIC or DHCP
        BOOL none       = (flags & 0x1) != 0;   // NONE
        
        if (configured && !none && a.ina != 0)
            return TRUE;
        if (GetTickCount() - t0 > ms)
            return FALSE;
        Sleep(500);
    }
}

// Decrypts stored network settings. Validates against garbage data.
static BOOL Load(CFG *c)
{
    memset(c, 0, sizeof(*c));
    pXnpLoadConfigParams(SYSAPP, c, 0, 0);
    return c->leaseSecs <= 30u * 24 * 3600 && c->flags < 0x1000;
}

static CFG g_cfg;

static void Run()
{
    // Phase 1: Wait for network and apply to SYSAPP (Dashboard)
    if (!WaitForAddress(SYSAPP, BOOT_WAIT))
        return;
    if (!Load(&g_cfg))
        return;

    g_cfg.dns[0] = GOOD_DNS1;
    g_cfg.dns[1] = GOOD_DNS2;
    pXnpConfig(SYSAPP, &g_cfg, 0);

    // Phase 2: Delay to allow stealth plugins (Proton/xbGuard) to hook xam
    Sleep(TITLE_DELAY);

    // Phase 3: Apply to TITLE (Games/Apps)
    pXnpConfig(TITLE, &g_cfg, 0);
    WaitForAddress(TITLE, SWAP_WAIT);
}

// No XNetCleanup: plugin stays resident. Tearing down context under xam's 
// caller id has undocumented refcount semantics.
static DWORD WINAPI Worker(LPVOID)
{
    if (!Resolve())
        return 0;

    BYTE startup[13] = { 13 };   // XNetStartupParams (size=13, defaults=0)
    if (pXNetStartup(SYSAPP, startup) != 0)
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
