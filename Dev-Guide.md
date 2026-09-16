# Xbox 360 Development Environment Setup Guide
### Debian Linux + Wine + Official XDK 21256.17

This guide documents how to set up a complete, native Xbox 360 development environment on a Debian-based Linux distribution using Wine and the official Microsoft Xbox 360 XDK (July 2012).

---

## 📦 Prerequisites & Downloads

### System Requirements
- Debian-based Linux distribution (tested on Debian Trixie)
- At least 10GB of free disk space
- \`wine\`, \`winetricks\`, and standard build tools installed

### Required Files (Archive.org Links)
You must download these two files before proceeding:

1. **Visual Studio 2010 Ultimate** (~2.3 GB)  
   *Required for the XDK compiler to install correctly.*  
   - Archive Page: https://archive.org/details/en_vs_2010_ult  
   - Direct Download: [SW_DVD9_VS_Ultimate_2010_English_Core_MLF_X16-76630.ISO](https://dn710006.ca.archive.org/0/items/en_vs_2010_ult/SW_DVD9_VS_Ultimate_2010_English_Core_MLF_X16-76630.ISO)

2. **Xbox 360 XDK 21256.17** (~1.4 GB)  
   *The last official SDK before Xbox 360 discontinuation.*  
   - Archive Page: https://archive.org/details/xdkcollection  
   - Direct Download: [XDKSetupXenon21256.17.exe](https://dn721906.ca.archive.org/0/items/xdkcollection/2013/21256.17/XDKSetupXenon21256.17.exe)

---

## 🛠️ Step 1: Install Wine and Dependencies

Open your terminal and run the following commands to prepare a clean, 32-bit Wine environment:

\`\`\`bash
# Update system and install Wine/Winetricks
sudo apt update && sudo apt upgrade -y
sudo apt install -y wine winetricks

# Configure a dedicated 32-bit Wine prefix for the XDK
export WINEPREFIX="$HOME/.wine-xbox360-xdk"
export WINEARCH=win32

# Initialize the prefix and open the configuration window
winecfg
\`\`\`

*In the Wine Configuration window:*
1. Go to the **Applications** tab.
2. Change **Windows Version** to **Windows 7**.
3. Click **Apply**, then **OK**.

Next, install the required Visual C++ runtime libraries:
\`\`\`bash
WINEPREFIX="$HOME/.wine-xbox360-xdk" winetricks -q vcrun2005 vcrun2008 vcrun2010 corefonts
\`\`\`
*(Accept all license agreements if prompted).*

---

## 💻 Step 2: Install Visual Studio 2010

The XDK installer requires Visual Studio to be present to install the compiler tools.

\`\`\`bash
# Create a mount point and mount the VS2010 ISO
sudo mkdir -p /mnt/vs2010
sudo mount -o loop ~/Downloads/SW_DVD9_VS_Ultimate_2010_English_Core_MLF_X16-76630.ISO /mnt/vs2010

# Run the installer
export WINEPREFIX="$HOME/.wine-xbox360-xdk"
export WINEARCH=win32
wine /mnt/vs2010/setup.exe
\`\`\`

*During Installation:*
1. Click **Install Microsoft Visual Studio 2010**.
2. Accept the license terms.
3. Choose **Custom** installation (Do NOT choose Full).
4. **Select ONLY:**
   - ✅ Microsoft Visual C++ 2010
   - ✅ Microsoft .NET Framework 4
5. **Uncheck everything else** (SQL Server, Silverlight, F#, Help Viewer) to save time and avoid Wine compatibility issues.
6. Click **Install** and wait (this may take 15–30 minutes).
7. If prompted to reboot at the end, click **No** or **Later**.

*Verify the compiler installed:*
\`\`\`bash
find "$WINEPREFIX/drive_c" -name "cl.exe" 2>/dev/null | head -1
\`\`\`

---

## 🎮 Step 3: Install Xbox 360 XDK 21256.17

Now that VS2010 is detected, the XDK will install the full toolchain.

\`\`\`bash
cd ~/Downloads
wine XDKSetupXenon21256.17.exe
\`\`\`

*During Installation:*
1. When prompted with Installation Options, choose **Full Installation** (Do NOT choose Minimum).
2. Keep the default path: \`C:\Program Files\Microsoft Xbox 360 SDK\`.
3. Click **Next** and wait for completion.
4. *Note:* Ignore cosmetic Wine errors in the terminal (e.g., \`NtUserChangeDisplaySettings\` or \`InvokeShellLinker\`). They do not affect the installation.

*Verify critical XDK files:*
\`\`\`bash
find "$WINEPREFIX/drive_c/Program Files/Microsoft Xbox 360 SDK" -name "xtl.h" 2>/dev/null
find "$WINEPREFIX/drive_c/Program Files/Microsoft Xbox 360 SDK" -name "xapilib.lib" 2>/dev/null
find "$WINEPREFIX/drive_c/Program Files/Microsoft Xbox 360 SDK" -name "imagexex.exe" 2>/dev/null
\`\`\`

---

## 🚀 Step 4: Build Your Project

This repository includes a dynamic \`build.sh\` script that handles the entire compilation pipeline.

1. Open \`build.sh\` in a text editor.
2. Modify the top two variables to match your target file and desired version:
   \`\`\`bash
   VERSION="v1.0.3-beta"
   SOURCE_FILE="AutoDNS-beta.cpp"
   \`\`\`
3. Save the file and make it executable (if not already):
   \`\`\`bash
   chmod +x build.sh
   \`\`\`
4. Run the build:
   \`\`\`bash
   ./build.sh
   \`\`\`

The script will automatically create a versioned folder (e.g., \`build-v1.0.3-beta/\`) and output the final \`AutoDNS-v1.0.3-beta.xex\` file there.

---

## 🔧 Troubleshooting

- **\`LINK : fatal error LNK1181: cannot open input file 'xam.lib'\`**  
  *Solution:* The XDK 21256.17 does not include \`xam.lib\`. Ensure your \`build.sh\` links against \`xboxkrnl.lib\` and \`xapilib.lib\` only.

- **\`IMAGEXEX : error IM1067: invalid load address for module type\`**  
  *Solution:* Ensure the \`-XEX:NO -ALIGN:128,4096\` flags are present in the \`link.exe\` command within \`build.sh\`.

- **Terminal spam: \`err:ole:CoReleaseMarshalData StdMarshal ReleaseMarshalData failed\`**  
  *Solution:* This is harmless Wine cleanup noise. The provided \`build.sh\` already suppresses this using \`2>/dev/null\` and cleans up the background process with \`wineserver -k\`.

- **XDK installs only "Minimum" and skips the compiler**  
  *Solution:* The XDK installer failed to detect Visual Studio. Ensure Step 2 was completed successfully, then rerun the XDK installer and select "Full Installation".

---

## 📤 Deployment

- **Via FTP/XBDM:** Upload the \`.xex\` file to your Xbox 360 (default credentials: \`xbox\` / \`xbox\`, port \`21\` or \`730\`). Place it in \`H1:/Plugins/\` (for DashLaunch) or \`E:\UDATA\...\default.xex\` (for standalone).
- **Via USB:** Copy the \`.xex\` to a FAT32-formatted USB drive and launch it via Aurora, FreeStyle Dash, or XeXMenu.
- **Via Xenia Emulator:** Drag and drop the \`.xex\` file directly into the Xenia window on your PC.

---
*Last updated: September 2026*  
*Tested on: Debian Trixie, Wine 9.x*
