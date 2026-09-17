# AutoDNS XEX Plugin (v1.0.3)

A DashLaunch sysdll plugin that replaces the intentionally dead DNS (`192.0.2.1`) stored in NAND with working DNS servers at runtime. Enables internet access for games and apps while keeping the console offline at the hardware level for blind boot safety.

## Key Features

-   **Dual Context Injection:** Applies DNS to both `SYSAPP` (Dashboard) and `TITLE` (Games/Apps) contexts independently.
-   **NAND-Safe:** Never modifies flash storage. Next boot starts offline automatically.
-   **Resilient Application:** Includes retry logic and stack readiness verification after each configuration change.
-   **Dead DNS Detection:** Only activates if the dead DNS is actually present; skips injection if already valid.
-   **Stealth Compatible:** Works alongside local stealth services (Proton, xbGuard) without bypassing their network hooks.
-   **Customizable Build:** DNS servers can be set at compile time via CLI arguments.

## Installation & Setup

1.  Set your Xbox 360 Network Settings to **Manual DNS** with both servers as `192.0.2.1`.
2.  Copy `AutoDNS.xex` to your USB drive or HDD.
3.  Add it to your `launch.ini` under `[Plugins]`.

### ⚠️ Critical: Plugin Load Order

The load order in `launch.ini` is mandatory for proper operation with stealth plugins:

```ini
[Plugins]
plugin1 = Usb:\Freestealth\Proto.xex    # Stealth MUST load first
plugin2 = Usb:\AutoDNS.xex              # AutoDNS loads second
```

> **Why?** Stealth plugins need to arm their network hooks before AutoDNS finalizes the network configuration. Reversing this order may result in unfiltered traffic or connection failures.

## Building from Source

Requires Microsoft Xbox 360 SDK (XDK 21256) and Wine.

```bash
# Default build (Cloudflare DNS)
./build.sh

# Custom DNS servers
./build.sh 8.8.8.8 8.8.4.4

# Custom TITLE_DELAY (milliseconds)
TITLE_DELAY=0 ./build.sh
```

Output will be placed in `build-beta6/AutoDNS-beta6.xex`. Rename to `AutoDNS.xex` before deploying.

## Technical Notes

-   Uses `XnpConfig` for runtime-only DNS replacement. No NAND writes.
-   Separate `CFG` structs per context prevent race conditions during async write-back.
-   `ApplyDNS()` retries up to 3 times with 5s intervals and verifies stack readiness via `XNetGetTitleXnAddr`.
-   Compiled with `-D NDEBUG`; no debug logging included in release binary.

## License

Licensed under the MIT License.
