================================================================================
XBOX 360 DEVELOPMENT ENVIRONMENT SETUP GUIDE
Debian Linux + Wine + XDK 21256.17
================================================================================

This guide documents how to set up a complete Xbox 360 development environment
on Debian Linux using Wine and the official Microsoft XDK 21256.17 (July 2012).

================================================================================
PREREQUISITES
================================================================================

System Requirements:
- Debian-based Linux distribution (tested on Debian Trixie)
- At least 10GB free disk space
- Internet connection for downloading dependencies

Required Files (obtain from Archive.org or other sources):
1. Visual Studio 2010 Ultimate (~2.3GB)
   File: SW_DVD9_VS_Ultimate_2010_English_Core_MLF_X16-76630.ISO

2. Visual Studio 2010 SP1 (~1.5GB) - Optional
   File: mu_visual_studio_2010_sp1_x86_dvd_651704.iso

3. Xbox 360 XDK 21256.17 (~1.4GB)
   File: XDKSetupXenon21256.17.exe

================================================================================
STEP 1: INSTALL WINE AND DEPENDENCIES
================================================================================

# Update system
sudo apt update
sudo apt upgrade -y

# Install Wine and Winetricks
sudo apt install -y wine winetricks

# Configure Wine for 32-bit (XDK requires 32-bit)
export WINEPREFIX="$HOME/.wine-xbox360-xdk"
export WINEARCH=win32

# Initialize Wine prefix and set Windows version to Windows 7
winecfg

In the Wine configuration window:
- Go to Applications tab
- Set Windows Version to Windows 7
- Click Apply then OK

================================================================================
STEP 2: INSTALL VISUAL C++ RUNTIME LIBRARIES
================================================================================

# Install required Visual C++ runtimes
WINEPREFIX="$HOME/.wine-xbox360-xdk" winetricks -q vcrun2005 vcrun2008 vcrun2010 corefonts

Accept all license agreements when prompted.

================================================================================
STEP 3: INSTALL VISUAL STUDIO 2010
================================================================================

# Mount the ISO
sudo mkdir -p /mnt/vs2010
sudo mount -o loop ~/Downloads/SW_DVD9_VS_Ultimate_2010_English_Core_MLF_X16-76630.ISO /mnt/vs2010

# Run the installer
export WINEPREFIX="$HOME/.wine-xbox360-xdk"
export WINEARCH=win32
wine /mnt/vs2010/setup.exe

INSTALLATION OPTIONS:
1. Click "Install Microsoft Visual Studio 2010"
2. Accept license terms
3. Choose CUSTOM installation (NOT Full)
4. SELECT ONLY:
   [X] Microsoft Visual C++ 2010
   [X] Microsoft .NET Framework 4
5. UNCHECK (to save time and avoid Wine issues):
   [ ] SQL Server
   [ ] Silverlight
   [ ] F#
   [ ] Crystal Reports
   [ ] Help Viewer
6. Click Install and wait (15-30 minutes)
7. If asked to reboot, click "No" or "Later"

# Verify installation
find "$WINEPREFIX/drive_c" -name "cl.exe" 2>/dev/null

Expected output should show paths like:
/home/user/.wine-xbox360-xdk/drive_c/Program Files/Microsoft Visual Studio 10.0/VC/bin/cl.exe

================================================================================
STEP 4: INSTALL XBOX 360 XDK 21256.17
================================================================================

# Run the XDK installer
cd ~/Downloads
wine XDKSetupXenon21256.17.exe

INSTALLATION OPTIONS:
1. When prompted, choose FULL INSTALLATION (NOT Minimum)
2. Keep default path: C:\Program Files\Microsoft Xbox 360 SDK
3. Click Next and wait for installation to complete
4. Ignore cosmetic Wine errors (display settings, icon extraction failures)

# Verify installation
find "$WINEPREFIX/drive_c/Program Files/Microsoft Xbox 360 SDK" -name "xtl.h" 2>/dev/null
find "$WINEPREFIX/drive_c/Program Files/Microsoft Xbox 360 SDK" -name "xboxkrnl.lib" 2>/dev/null
find "$WINEPREFIX/drive_c/Program Files/Microsoft Xbox 360 SDK" -name "imagexex.exe" 2>/dev/null

Expected output:
/home/user/.wine-xbox360-xdk/drive_c/Program Files/Microsoft Xbox 360 SDK/include/xbox/xtl.h
/home/user/.wine-xbox360-xdk/drive_c/Program Files/Microsoft Xbox 360 SDK/lib/xbox/xboxkrnl.lib
/home/user/.wine-xbox360-xdk/drive_c/Program Files/Microsoft Xbox 360 SDK/bin/win32/imagexex.exe

================================================================================
STEP 5: BUILD YOUR PROJECT
================================================================================

Navigate to your project directory:
cd ~/Xbox360AutoDNS

Run the build script:
chmod +x build.sh
./build.sh

Expected output:
🛠️ Compiling with XDK 21256 via Wine...
   -> Compiling...
   -> Linking...
   -> Generating .xex...
✅ SUCCESS! Plugin generated:
-rw-r--r-- 1 user user 20K Sep 16 09:40 build/AutoDNS-beta.xex

================================================================================
TROUBLESHOOTING
================================================================================

ERROR: LINK : fatal error LNK1181: cannot open input file 'xam.lib'
SOLUTION: Remove xam.lib from the linker command. Use only xboxkrnl.lib 
          and xapilib.lib (the XDK 21256 doesn't include xam.lib).

ERROR: IMAGEXEX : error IM1067: invalid load address for module type
SOLUTION: Add -XEX:NO -ALIGN:128,4096 flags to the link.exe command.

ERROR: Setup has detected that this computer does not meet the requirements
SOLUTION: You're trying to install VS2010 SP1 without VS2010 base installed.
          Install the full VS2010 Ultimate ISO first.

ERROR: Terminal spam with err:ole:CoReleaseMarshalData
SOLUTION: This is harmless Wine cleanup noise. Add 2>/dev/null to wine 
          commands or run wineserver -k after build.

ERROR: XDK installs only "Minimum" and skips compiler
SOLUTION: XDK requires Visual Studio 2010 to be detected. Install VS2010 
          first, then reinstall XDK and choose "Full Installation".

================================================================================
IMPORTANT NOTES
================================================================================

1. The XDK 21256.17 is the last official SDK before Xbox 360 discontinuation.

2. Wine errors are cosmetic and don't affect the final .xex output.

3. Always test plugins on a modded Xbox 360 (RGH/JTAG) - retail consoles 
   won't run unsigned code.

4. Keep the original AutoDNS.cpp as fallback if Beta causes crashes.

5. The WINEPREFIX is set to ~/.wine-xbox360-xdk to keep it isolated from 
   other Wine applications.

================================================================================
DEPLOYMENT OPTIONS
================================================================================

VIA FTP/XBDM:
- Connect to Xbox 360 (default credentials: xbox/xbox, port 21 or 730)
- Upload build/AutoDNS-beta.xex to:
  * E:\UDATA\AutoDNS-beta\default.xex (standalone app)
  * F:\Plugins\AutoDNS-beta.xex (DashLaunch plugin)

VIA USB:
1. Copy build/AutoDNS-beta.xex to FAT32 USB drive
2. Launch via Aurora, FreeStyle Dash, or XeXMenu on Xbox 360

VIA XENIA EMULATOR:
1. Download Xenia from https://xenia.jp/
2. Drag build/AutoDNS-beta.xex into Xenia window

================================================================================
CREDITS
================================================================================

- Microsoft Xbox 360 XDK 21256.17 (July 2012)
- Wine Project (winehq.org)
- AutoDNS Project Contributors

================================================================================
Last updated: September 2026
Tested on: Debian Trixie, Wine 9.x
================================================================================
