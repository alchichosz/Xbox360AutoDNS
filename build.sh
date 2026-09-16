#!/usr/bin/env bash
set -e

# Configurar ambiente Wine para Xbox 360 XDK
export WINEPREFIX="$HOME/.wine-xbox360-xdk"
export WINEARCH=win32

# Caminhos do XDK dentro do Wine
XEDK="C:\\Program Files\\Microsoft Xbox 360 SDK"
BIN="$XEDK\\bin\\win32"
INC="$XEDK\\include\\xbox"
LIB="$XEDK\\lib\\xbox"

echo "️ Compilando AutoDNS-beta.cpp com XDK 21256 via Wine..."
mkdir -p build

# 1. Compilar com cl.exe
echo "   -> Compilando..."
wine "$BIN\\cl.exe" -nologo -c -W4 -Ox -MT -GR- -EHsc -TP \
    -D _XBOX -D NDEBUG \
    -I"$INC" \
    -Fobuild\\AutoDNS-beta.obj AutoDNS-beta.cpp

# 2. Linkar com link.exe (com flags XEX corretas)
echo "   -> Linkando..."
wine "$BIN\\link.exe" -nologo -RELEASE -OPT:REF -DLL -ENTRY:_DllMainCRTStartup \
    -XEX:NO -ALIGN:128,4096 \
    -LIBPATH:"$LIB" \
    -OUT:build\\AutoDNS-beta.exe build\\AutoDNS-beta.obj \
    xboxkrnl.lib xapilib.lib

# 3. Converter para XEX usando imagexex.exe
echo "   -> Gerando .xex..."
wine "$BIN\\imagexex.exe" -nologo -config:AutoDNS.xex.xml -out:build\\AutoDNS-beta.xex build\\AutoDNS-beta.exe

echo "✅ SUCESSO! Plugin gerado:"
ls -lh build/AutoDNS-beta.xex
