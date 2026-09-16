#!/usr/bin/env bash
set -e

# ==============================================================================
# BUILD CONFIGURATION (CHANGE THESE FOR EACH NEW VERSION)
# ==============================================================================
VERSION="v1.0.3-beta"          # e.g., v1.0.3, v1.1.0, test-01, etc.
SOURCE_FILE="AutoDNS-beta.cpp" # The .cpp file to be compiled

# The script generates names automatically based on the variables above
BUILD_DIR="build-${VERSION}"
OUTPUT_NAME="AutoDNS-${VERSION}"
# ==============================================================================

# Configure Wine environment for Xbox 360 XDK
export WINEPREFIX="$HOME/.wine-xbox360-xdk"
export WINEARCH=win32

# XDK paths inside Wine
XEDK="C:\\Program Files\\Microsoft Xbox 360 SDK"
BIN="$XEDK\\bin\\win32"
INC="$XEDK\\include\\xbox"
LIB="$XEDK\\lib\\xbox"

echo "============================================================"
echo "🛠️  Compiling: $SOURCE_FILE"
echo "📦 Version:    $VERSION"
echo "📁 Output:     $BUILD_DIR/"
echo "============================================================"

# Create specific build directory for this version
mkdir -p "$BUILD_DIR"

# 1. Compile with cl.exe
echo "   -> [1/3] Compiling object..."
wine "$BIN\\cl.exe" -nologo -c -W4 -Ox -MT -GR- -EHsc -TP \
    -D _XBOX -D NDEBUG \
    -I"$INC" \
    -Fo"$BUILD_DIR\\${OUTPUT_NAME}.obj" "$SOURCE_FILE" 2>/dev/null

# 2. Link with link.exe
echo "   -> [2/3] Linking executable..."
wine "$BIN\\link.exe" -nologo -RELEASE -OPT:REF -DLL -ENTRY:_DllMainCRTStartup \
    -XEX:NO -ALIGN:128,4096 \
    -LIBPATH:"$LIB" \
    -OUT:"$BUILD_DIR\\${OUTPUT_NAME}.exe" "$BUILD_DIR\\${OUTPUT_NAME}.obj" \
    xboxkrnl.lib xapilib.lib 2>/dev/null

# 3. Convert to XEX using imagexex.exe
echo "   -> [3/3] Generating .xex file..."
wine "$BIN\\imagexex.exe" -nologo -config:AutoDNS.xex.xml \
    -out:"$BUILD_DIR\\${OUTPUT_NAME}.xex" "$BUILD_DIR\\${OUTPUT_NAME}.exe" 2>/dev/null

# Kill wineserver to prevent terminal spam
wineserver -k 2>/dev/null

echo "============================================================"
echo "✅ SUCCESS! Build completed."
echo "📦 Final file: $BUILD_DIR/${OUTPUT_NAME}.xex"
ls -lh "$BUILD_DIR/${OUTPUT_NAME}.xex"
echo "============================================================"
