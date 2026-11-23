#!/usr/bin/env bash
# Cross-compiles Snowy.exe on Linux with MinGW-w64 (x86_64).
#   sudo apt install g++-mingw-w64-x86-64-posix mingw-w64-tools
#   ./build.sh            -> build/Snowy.exe
set -euo pipefail
cd "$(dirname "$0")"
CXX=${CXX:-x86_64-w64-mingw32-g++-posix}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
OUT=build
mkdir -p "$OUT/obj"
FLAGS="-std=c++17 -O2 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -Wall -Wno-unknown-pragmas -Wno-missing-field-initializers"
OBJS=()
for f in src/*.cpp; do
  o="$OUT/obj/$(basename "${f%.cpp}").o"
  if [[ ! -f "$o" || "$f" -nt "$o" || src/shaders.h -nt "$o" || src/world.h -nt "$o" || src/renderer.h -nt "$o" || src/common.h -nt "$o" || src/engine.h -nt "$o" || src/ui.h -nt "$o" ]]; then
    echo "  CXX $f"
    $CXX $FLAGS -c "$f" -o "$o" &
  fi
  OBJS+=("$o")
done
wait
$WINDRES -I res res/snowy.rc -O coff -o "$OUT/obj/snowy_res.o"
$CXX -municode -mwindows -static -static-libgcc -static-libstdc++ -o "$OUT/Snowy.exe" "${OBJS[@]}" "$OUT/obj/snowy_res.o" \
  -ld3d11 -ldxgi -ldwmapi -lshcore -lcomctl32 -lole32 -luuid -lwindowscodecs -lgdi32 -luser32 -lshell32 -ladvapi32 -lwinmm -lwtsapi32 -lmsimg32
x86_64-w64-mingw32-strip "$OUT/Snowy.exe" 2>/dev/null || true
ls -la "$OUT/Snowy.exe"
