// ============================================================================
// AutoDNS v1.0.4 (Final Release)
// ============================================================================
// A DashLaunch sysdll plugin for Xbox 360 (Kernel 17559 / Corona and others).
//
// FEATURES & BEHAVIOR:
// 1. Native Toast Notifications: Bypasses XAM system-thread restrictions by 
//    spawning a dedicated USER thread via CreateThread().
// 2. Stealth UI Timing: Delays the boot notification to prevent race conditions
//    with pre-login stealth hooks (xbGuard).
// 3. Passive Watchdog: Detects and recovers from runtime DNS reversion.
// 4. Safe NAND Handling: Checks live stack flags (XADDR_DNS) before reading 
//    the NAND config, preventing false positives.
//
// SETUP: Network Settings -> DNS Manual -> 192.0.2.1 for both servers.
// ============================================================================

#include <xtl.h>
#include <stddef.h>
#include <string.h>

// --- Debug/Test Flags ------------------------------------------------------
// TEST MODE: Uncomment to force "Restored" notify 15s after boot.
// #define TEST_RESTORE_NOTIFY 

// --- Conditional Logging ---------------------------------------------------
#ifdef AUTOLOG
#define LOG(fmt, ...) DbgPrint("[AutoDNS] " fmt, ##__VA_ARGS__)
#else
#define LOG(fmt, ...)
#endif

// --- DNS configuration ---------------------------------------------------
#define DEAD_DNS           0xC0000201u  // 192.0.2.1 (RFC 5737, never routed)
#ifndef GOOD_DNS1
#define GOOD_DNS1          0x01010101u  // 1.1.1.1 (Cloudflare default)
#endif
#ifndef GOOD_DNS2
#define GOOD_DNS2          0x01000001u  // 1.0.0.1 (Cloudflare default)
#endif

// --- Boot & UI Timing ------------------------------------------------------
#define BOOT_WAIT          75000        // ms: wait for DHCP/Wi-Fi at boot
#define SWAP_WAIT          30000        // ms: wait for stack after XnpConfig
#define TITLE_DELAY        8000         // ms: safety margin before TITLE context
#define BOOT_NOTIFY_DELAY  15000         // ms: DELAY UI TOAST to let xbGuard finish its hooks!
#define APPLY_RETRIES      3            
#define APPLY_RETRY_WAIT   5000         

// --- Watchdog timing -----------------------------------------------------
#define WATCHDOG_SNOOZE    60000        
#define WATCHDOG_INTERVAL  15000        
#define WATCHDOG_TIMEOUT   180000       

// --- Network caller contexts ---------------------------------------------
#define SYSAPP 2                        
#define TITLE  1                        

// --- XNetGetTitleXnAddr status flags -------------------------------------
#define XADDR_NONE         0x01
#define XADDR_ETHERNET     0x02
#define XADDR_STATIC       0x04
#define XADDR_DHCP         0x08
#define XADDR_PPPOE        0x10
#define XADDR_GATEWAY      0x20
#define XADDR_DNS          0x40
#define XADDR_ONLINE       0x80

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

C_ASSERT(sizeof(CFG)              == 492);
C_ASSERT(offsetof(CFG, flags)     == 0x4C);
C_ASSERT(offsetof(CFG, dns)       == 0x60);
C_ASSERT(offsetof(CFG, leaseSecs) == 0x168);

// --- xam.xex exports -----------------------------------------------------
static int   (*pXNetStartup)(int, BYTE *);
static DWORD (*pXNetGetTitleXnAddr)(int, XNADDR_ *);
static int   (*pXnpConfig)(int, CFG *, DWORD);
static int   (*pXnpLoadConfigParams)(int, CFG *, DWORD, DWORD);

typedef VOID (*XNOTIFYQUEUEUI)(
    DWORD type,
    DWORD userIndex,
    ULONGLONG qwAreas,
    LPCWSTR displayText,
    PVOID pContextData
);

static XNOTIFYQUEUEUI pXNotifyQueueUI = NULL;

// --- Per-context CFG structs ---------------------------------------------
static CFG  g_cfg_sys;
static CFG  g_cfg_title;
static CFG  g_cfg_watchdog;

static BYTE g_startup[13] = { 13 };
static volatile BOOL g_boot_complete = FALSE;

// -------------------------------------------------------------------------
static BOOL Resolve()
{
    HANDLE xam;
    if (XexGetModuleHandle("xam.xex", &xam) < 0)
        return FALSE;

    struct { DWORD ord; PVOID *fn; } required[] = {
        {  51, (PVOID *)&pXNetStartup         },
        {  73, (PVOID *)&pXNetGetTitleXnAddr  },
        { 101, (PVOID *)&pXnpLoadConfigParams },
        { 104, (PVOID *)&pXnpConfig           },
    };
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (XexGetProcedureAddress(xam, required[i].ord, required[i].fn) < 0 ||
            *required[i].fn == NULL)
            return FALSE;
    }

    XexGetProcedureAddress(xam, 0x290, (PVOID *)&pXNotifyQueueUI);
    return TRUE;
}

// -------------------------------------------------------------------------
static DWORD WINAPI NotifyThreadProc(LPVOID lpParam)
{
    if (!pXNotifyQueueUI) {
        delete[] (wchar_t*)lpParam;
        return 0;
    }

    LPCWSTR msg = (LPCWSTR)lpParam;
    Sleep(250); // Let USER thread context stabilize

    pXNotifyQueueUI(
        3,                  // XNOTIFYUI_TYPE_GENERIC
        0xFF,               // XUSER_INDEX_ANY
        0x00000001ULL,      // XNOTIFY_SYSTEM (ULONGLONG for PPC ABI)
        msg,
        NULL
    );

    delete[] (wchar_t*)lpParam;
    return 0;
}

// -------------------------------------------------------------------------
static void ShowNotification(const char* msg)
{
    if (!pXNotifyQueueUI) return;

    wchar_t* threadMsg = new wchar_t[128];
    if (!threadMsg) return;

    int i = 0;
    for (; i < 127 && msg[i] != '\0'; i++)
        threadMsg[i] = (wchar_t)msg[i];
    threadMsg[i] = L'\0';

    HANDLE hNotify = CreateThread(
        NULL, 0, NotifyThreadProc, (LPVOID)threadMsg, CREATE_SUSPENDED, NULL
    );

    if (hNotify) {
        ResumeThread(hNotify);
        CloseHandle(hNotify);
    } else {
        delete[] threadMsg;
    }
}

// -------------------------------------------------------------------------
// TEST THREAD: Forces the "Restored" notify to appear for testing purposes.
// -------------------------------------------------------------------------
#ifdef TEST_RESTORE_NOTIFY
static DWORD WINAPI TestRestoreThread(LPVOID)
{
    LOG("Test: Waiting 15s to force 'Restored' notify...\n");
    Sleep(15000);
    ShowNotification("AutoDNS: Restored (TEST)");
    return 0;
}
#endif

// -------------------------------------------------------------------------
// (Proven, stable DNS and Watchdog logic below)
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

        if (hasNet && hasDns && hasIp && !none) return TRUE;
        if (GetTickCount() - t0 > ms) return FALSE;
        Sleep(500);
    }
}

static BOOL Load(CFG *dst)
{
    memset(dst, 0, sizeof(*dst));
    pXnpLoadConfigParams(SYSAPP, dst, 0, 0);
    return dst->leaseSecs <= 30u * 24 * 3600 && dst->flags < 0x1000;
}

static BOOL StartContext(int caller)
{
    return (pXNetStartup(caller, g_startup) == 0);
}

static BOOL ApplyDNS(int caller, CFG *cfg)
{
    for (int attempt = 1; attempt <= APPLY_RETRIES; attempt++) {
        if (pXnpConfig(caller, cfg, 0) == 0 && WaitForAddress(SWAP_WAIT, caller))
            return TRUE;
        Sleep(APPLY_RETRY_WAIT);
    }
    return FALSE;
}

static void WatchdogReapply()
{
    CFG sys_copy;
    CFG title_copy;
    memcpy(&sys_copy,   &g_cfg_sys,   sizeof(CFG));
    memcpy(&title_copy, &g_cfg_title, sizeof(CFG));

    LOG("Watchdog: Reapplying DNS...\n");
    ShowNotification("AutoDNS: Restored");

    ApplyDNS(SYSAPP, &sys_copy);
    ApplyDNS(TITLE,  &title_copy);
}

static DWORD WINAPI WatchdogWorker(LPVOID)
{
    DWORD waitStart = GetTickCount();
    while (!g_boot_complete) {
        if (GetTickCount() - waitStart > WATCHDOG_TIMEOUT) {
            LOG("Watchdog: Boot timeout. Aborting.\n");
            return 0;
        }
        Sleep(1000);
    }
    LOG("Watchdog: Active.\n");

    Sleep(WATCHDOG_SNOOZE);

    for (;;) {
        Sleep(WATCHDOG_INTERVAL);

        XNADDR_ a;
        memset(&a, 0, sizeof(a));
        DWORD flags = pXNetGetTitleXnAddr(SYSAPP, &a);

        BOOL hasIp  = (a.ina != 0);
        BOOL hasDns = (flags & XADDR_DNS) != 0;
        BOOL hasNet = (flags & (XADDR_STATIC | XADDR_DHCP)) != 0;
        BOOL none   = (flags & XADDR_NONE) != 0;

        if (hasNet && hasDns && hasIp && !none) continue;
        if (!hasNet || none) continue;
        if (!Load(&g_cfg_watchdog)) continue;

        if (g_cfg_watchdog.dns[0] == DEAD_DNS || g_cfg_watchdog.dns[1] == DEAD_DNS)
            WatchdogReapply();
    }
    return 0;
}

static void Run()
{
    LOG("AutoDNS: Starting boot sequence.\n");

    if (!WaitForAddress(BOOT_WAIT, SYSAPP)) {
        LOG("AutoDNS: SYSAPP timeout.\n");
        return;
    }
    if (!Load(&g_cfg_sys)) {
        LOG("AutoDNS: NAND load failed.\n");
        return;
    }

    if (g_cfg_sys.dns[0] != DEAD_DNS && g_cfg_sys.dns[1] != DEAD_DNS) {
        LOG("AutoDNS: DNS already valid.\n");
        g_boot_complete = TRUE;
        return;
    }

    g_cfg_sys.dns[0] = GOOD_DNS1;
    g_cfg_sys.dns[1] = GOOD_DNS2;

    LOG("AutoDNS: Applying SYSAPP.\n");
    if (!ApplyDNS(SYSAPP, &g_cfg_sys)) {
        LOG("AutoDNS: SYSAPP failed.\n");
        return;
    }

#if TITLE_DELAY > 0
    Sleep(TITLE_DELAY);
#endif

    memcpy(&g_cfg_title, &g_cfg_sys, sizeof(CFG));

    LOG("AutoDNS: Starting TITLE context.\n");
    if (!StartContext(TITLE)) {
        LOG("AutoDNS: TITLE context failed.\n");
        return;
    }

    LOG("AutoDNS: Applying TITLE.\n");
    if (!ApplyDNS(TITLE, &g_cfg_title)) {
        LOG("AutoDNS: TITLE failed.\n");
        return;
    }

    LOG("AutoDNS: Boot complete.\n");

    // CRITICAL FIX FOR XBGUARD: 
    // Wait 9 seconds AFTER DNS is applied to let xbGuard finish its UI hooks 
    // and avoid XAM queue collisions with its "online" notification.
    Sleep(BOOT_NOTIFY_DELAY);

    ShowNotification("AutoDNS: Active");

#ifdef TEST_RESTORE_NOTIFY
    // Spawn test thread to verify the "Restored" notify works
    HANDLE hTest = CreateThread(NULL, 0, TestRestoreThread, NULL, 0, NULL);
    if (hTest) CloseHandle(hTest);
#endif

    g_boot_complete = TRUE;
}

static DWORD WINAPI Worker(LPVOID)
{
    if (!Resolve()) {
        LOG("AutoDNS: Export resolution failed.\n");
        return 0;
    }
    if (!StartContext(SYSAPP)) {
        LOG("AutoDNS: SYSAPP context failed.\n");
        return 0;
    }
    Run();
    return 0;
}

extern "C" BOOL WINAPI DllMain(HANDLE, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    HANDLE h1 = NULL;
    if (ExCreateThread(&h1, 0, NULL, NULL, Worker, NULL, 2) >= 0 && h1 != NULL)
        CloseHandle(h1);

    HANDLE h2 = NULL;
    if (ExCreateThread(&h2, 0, NULL, NULL, WatchdogWorker, NULL, 2) >= 0 && h2 != NULL)
        CloseHandle(h2);

    return TRUE;
}
