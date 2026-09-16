# AutoDNS for Xbox 360

AutoDNS is a custom DNS server plugin designed for the Xbox 360 (specifically for RGH/JTAG modified consoles). It intercepts and manages DNS requests to control network behavior, block telemetry, or redirect traffic.

## Versions

- **\`AutoDNS.cpp\`**: The stable, original version (v1.0.2).
- **\`AutoDNS-beta.cpp\`**: The experimental Beta version. Features advanced **Stealth** (packet obfuscation/evasion) and **Server Delay** (controlled response timing to prevent timeouts and rate-limiting).

## Project Structure

\`\`\`
.
├── AutoDNS.cpp          # Stable version source code
├── AutoDNS-beta.cpp     # Beta version source code (Stealth + Delay)
├── AutoDNS.xex.xml      # XEX module configuration
├── build.sh             # Automated build script (Wine + XDK 21256)
├── Dev-Guide.txt        # Step-by-step environment setup guide
└── build/               # Compiled artifacts (gitignored)
    └── AutoDNS-beta.xex # Final executable
\`\`\`

## How to Build

This project is compiled natively on Linux using the official **Microsoft Xbox 360 XDK (21256.17)** running through **Wine**.

### Prerequisites

You must have the XDK 21256.17 and Visual Studio 2010 installed in a dedicated 32-bit Wine prefix.

### Compilation Steps

1. Ensure you have the \`WINEPREFIX\` configured for the Xbox 360 XDK.
2. Run the build script:
   \`\`\`bash
   chmod +x build.sh
   ./build.sh
   \`\`\`
3. The final plugin will be generated at \`build/AutoDNS-beta.xex\`.

## Environment Setup

Setting up the Xbox 360 XDK on Linux can be tricky. For a complete, step-by-step guide on how to install Wine, Visual Studio 2010, and the XDK 21256.17 on Debian, please read the **\`Dev-Guide.txt\`** file included in this repository.

## Deployment

1. **Via FTP/XBDM:** Upload \`build/AutoDNS-beta.xex\` to your Xbox 360.
   - For DashLaunch plugins: \`H1:/Plugins/\` or \`F:\\Plugins\\\`
   - For standalone apps: \`E:\\UDATA\\AutoDNS\\default.xex\`
2. **Via USB:** Copy the \`.xex\` to a FAT32 drive and launch via Aurora, FreeStyle Dash, or XeXMenu.

## Troubleshooting

- **LNK1181 (cannot open xam.lib):** The XDK 21256 doesn't include \`xam.lib\`. The \`build.sh\` is already configured to link only \`xboxkrnl.lib\` and \`xapilib.lib\`.
- **IMAGEXEX IM1067 (invalid load address):** Ensure the \`-XEX:NO -ALIGN:128,4096\` flags are present in the \`link.exe\` command inside \`build.sh\`.

## Credits

- Original AutoDNS Project
- Microsoft Xbox 360 XDK 21256.17
- Wine Project
