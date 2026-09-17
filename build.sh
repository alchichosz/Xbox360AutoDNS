#!/usr/bin/env bash
# Builds AutoDNS-beta.xex with the Xbox 360 XDK via Wine.
#
# Usage: ./build.sh [DNS1] [DNS2]
# Example: ./build.sh 8.8.8.8 8.8.4.4
set -e

# Build configuration
VERSION="beta6"
SOURCE_FILE="AutoDNS-beta.cpp" 
BUILD_DIR="build-${VERSION}"
OUTPUT_NAME="AutoDNS-${VERSION}"

# Configure Wine environment
export WINEPREFIX="$HOME/.wine-xbox360-xdk"
export WINEARCH=win32

# XDK paths (Windows format for Wine)
XEDK="C:\\Program Files\\Microsoft Xbox 360 SDK"
BIN="$XEDK\\bin\\win32"
INC="$XEDK\\include\\xbox"
LIB="$XEDK\\lib\\xbox"

# Dotted quad -> 0xAABBCCDD (network order / PowerPC byte order)
hex_ip() {
    [[ "$1" =~ ^([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})\.([0-9]{1,3})$ ]] || { 
        echo "Error: not a valid IPv4 address: $1" >&2; exit 1; 
    }
    for o in "${BASH_REMATCH[@]:1}"; do 
        [ "$o" -le 255 ] || { echo "Error: octet out of range: $1" >&2; exit 1; }; 
    done
    printf '0x%02X%02X%02X%02Xu' "${BASH_REMATCH[@]:1}"
}

# Default to Cloudflare if no arguments provided
DNS1="${1:-1.1.1.1}"
DNS2="${2:-1.0.0.1}"
HEX1=$(hex_ip "$DNS1")
HEX2=$(hex_ip "$DNS2")

echo "============================================================"
echo "Building: AutoDNS-beta.cpp"
echo "DNS 1: $DNS1 ($HEX1)"
echo "DNS 2: $DNS2 ($HEX2)"
echo "Output:  $BUILD_DIR/${OUTPUT_NAME}.xex"
echo "============================================================"

mkdir -p "$BUILD_DIR"

# 1. Compile
echo "   -> [1/3] Compiling..."
wine "$BIN\\cl.exe" -nologo -c -W4 -Ox -MT -GR- -EHsc -TP \
    -D _XBOX -D NDEBUG -D "GOOD_DNS1=$HEX1" -D "GOOD_DNS2=$HEX2" \
    -I"$INC" \
    -Fo"$BUILD_DIR\\${OUTPUT_NAME}.obj" "$SOURCE_FILE" 2>/dev/null

# 2. Link
echo "   -> [2/3] Linking..."
wine "$BIN\\link.exe" -nologo -RELEASE -OPT:REF -DLL -ENTRY:_DllMainCRTStartup \
    -XEX:NO -ALIGN:128,4096 \
    -LIBPATH:"$LIB" \
    -OUT:"$BUILD_DIR\\${OUTPUT_NAME}.exe" "$BUILD_DIR\\${OUTPUT_NAME}.obj" \
    xboxkrnl.lib xapilib.lib 2>/dev/null

# 3. Generate XEX
echo "   -> [3/3] Generating XEX..."
wine "$BIN\\imagexex.exe" -nologo -config:AutoDNS.xex.xml \
    -out:"$BUILD_DIR\\${OUTPUT_NAME}.xex" "$BUILD_DIR\\${OUTPUT_NAME}.exe" 2>/dev/null

# Cleanup wineserver
wineserver -k 2>/dev/null

echo "============================================================"
echo "SUCCESS! Build completed."
ls -lh "$BUILD_DIR/${OUTPUT_NAME}.xex"
echo "============================================================"
