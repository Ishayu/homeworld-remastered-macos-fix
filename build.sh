#!/bin/sh
# Build the 32-bit opengl32.dll shim with LLVM (clang + lld) and package it as dist/hwgl-homeworldrm.zip.
# No Windows SDK or MinGW needed.
#   LLVM=/path/to/llvm/bin ./build.sh        (defaults to Homebrew's llvm/lld on macOS)
set -eu
cd "$(dirname "$0")"

LLVM="${LLVM:-/opt/homebrew/opt/llvm/bin}"
LLD="${LLD:-$(command -v lld-link || echo /opt/homebrew/opt/lld/bin/lld-link)}"
OUT=build
TARGET=i686-pc-windows-msvc
CFLAGS="--target=$TARGET -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -Wall -Wextra"

mkdir -p "$OUT"
python3 gen_stubs.py "$OUT"

cat > "$OUT/kernel32.def" <<'EOF'
LIBRARY kernel32.dll
EXPORTS
LoadLibraryA@4
GetProcAddress@8
GetSystemDirectoryA@8
OutputDebugStringA@4
EOF
"$LLVM/llvm-dlltool" -m i386 -k -d "$OUT/kernel32.def" -l "$OUT/kernel32.lib"

"$LLVM/clang" $CFLAGS -c hwgl.c -o "$OUT/hwgl.obj"
"$LLVM/clang" --target=$TARGET -c "$OUT/stubs.S" -o "$OUT/stubs.obj"

"$LLD" /nologo /dll /machine:x86 /nodefaultlib /entry:DllMain /subsystem:windows /safeseh:no \
    /def:"$OUT/opengl32.def" /out:"$OUT/opengl32.dll" \
    "$OUT/hwgl.obj" "$OUT/stubs.obj" "$OUT/kernel32.lib"

echo "built $OUT/opengl32.dll"

# Installable package: everything a player needs, nothing they don't.
PKG=hwgl-homeworldrm
DIST=dist
rm -rf "$DIST"
mkdir -p "$DIST/$PKG/launchers"
cp "$OUT/opengl32.dll" homeworldrm-override.reg README.md LICENSE "$DIST/$PKG/"
cp launchers/*.bat "$DIST/$PKG/launchers/"
(cd "$DIST" && zip -qr "$PKG.zip" "$PKG")

echo "packaged $DIST/$PKG.zip"
