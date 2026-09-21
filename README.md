# AutoDNS XEX Plugin (v1.0.4)

A lightweight, high-performance DashLaunch `sysdll` plugin for Xbox 360 (Kernel 17559 / Corona and others). AutoDNS automatically injects custom DNS servers at boot and features an intelligent, passive background watchdog to prevent runtime DNS reversion without requiring a console reboot.

## Key Features

-   **Dual Context Injection:** Applies DNS to both `SYSAPP` (Dashboard) and `TITLE` (Games/Apps) contexts independently.
-   **Resilient Application:** Includes retry logic and stack readiness verification after each configuration change.
-   **Dead DNS Detection:** Only activates if the dead DNS is actually present; skips injection if already valid.
-   **Customizable Build:** DNS servers can be set at compile time via CLI arguments.
-   **Passive Watchdog:** Continuously monitors the live network stack. If the dashboard reverts the DNS (e.g., after closing the DashLaunch menu), the watchdog silently reapplies the correct DNS without dropping the active connection.
-   **Safe NAND Handling:** The plugin only *reads* the NAND configuration to verify the fallback state. It never writes to storage, ensuring your console remains in a stealth-friendly, offline state on the next boot.
-   **Native Toast Notifications:** Utilizes `XNotifyQueueUI` via a dedicated User Thread to display clean, non-blocking system notifications ("AutoDNS: Active" and "AutoDNS: Restored") without interfering with dashboard UI queues.
-   **Stealth-Optimized Timing:** Includes a built-in delay for UI notifications to prevent race conditions with pre-login stealth hooks (xbGuard/Proton).

## Installation & Setup

1. On your Xbox 360, go to **Network Settings** -> **Advanced Settings**.
2. Set **DNS Settings** to **Manual**.
3. Set both the **Primary** and **Secondary** DNS to: `192.0.2.1` *(This is a reserved, non-routable IP address per RFC 5737, ensuring the console starts offline and triggers the plugin).*
4. Place the compiled `AutoDNS.xex` on your storage device and add it to your `launch.ini` (remembering the critical load order above).

### ⚠️ Critical: Plugin Load Order

To ensure maximum stability and stealth, **you must load your stealth server plugin FIRST**, followed by AutoDNS. 
The load order in `launch.ini` is mandatory for proper operation with stealth plugins:

```ini
[Plugins]
plugin1 = Usb:\Freestealth\Proto.xex    # Stealth MUST load first
plugin2 = Usb:\AutoDNS.xex              # AutoDNS loads second
```

> **Why?** Stealth plugins need to arm their network hooks before AutoDNS finalizes the network configuration. Reversing this order may result in unfiltered traffic or connection failures.
*Reasoning:* This guarantees that all pre-login protection hooks are fully established by the stealth plugin before AutoDNS initiates any network contact or UI notifications.


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

Example: Output will be placed in `build-beta/AutoDNS-beta.xex`. Rename to `AutoDNS.xex` before deploying.

## Technical Notes

-   Uses `XnpConfig` for runtime-only DNS replacement. No NAND writes.
-   Separate `CFG` structs per context prevent race conditions during async write-back.
-   `ApplyDNS()` retries up to 3 times with 5s intervals and verifies stack readiness via `XNetGetTitleXnAddr`.
-   Compiled with `-D NDEBUG`; no debug logging included in release binary.
---
**Disclaimer: This tool is intended for educational purposes and legitimate network configuration on modified consoles. Use at your own risk.**

## License

Licensed under the MIT License.
