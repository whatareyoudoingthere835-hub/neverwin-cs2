#!/usr/bin/env bash
# ============================================================================
# Сборка QUINT release vN (Linux, кросс-компиляция через zig + mingw-w64).
# Исходник: quint/ (quintcs2.zip от passtuh) с оффсетами дампа 14176.
# Объекты кэшируются в quint/.build/objs — повторная сборка быстрая.
# Менять исходники quint — снести quint/.build (или ./build_release.sh --clean).
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$PWD"

SRC=quint
OUT=release
ZIG="${ZIG:-zig}"
V="${1:-}"
if [[ "$V" == "--clean" ]]; then rm -rf "$SRC/.build"; V=""; fi

if [[ -z "$V" ]]; then
    V=1
    for f in "$OUT"/quint_v*.dll; do
        [[ -e "$f" ]] || continue
        n="${f##*quint_v}"; n="${n%%.dll}"
        if (( n >= V )); then V=$((n + 1)); fi
    done
fi
[[ "$V" =~ ^[0-9]+$ ]] || { echo "версия должна быть числом"; exit 2; }

WORK="$REPO/$SRC/.build"
mkdir -p "$WORK/objs" "$WORK/stublibs" "$WORK/shims"

# Шимы: Windows.h -> windows.h, стандартные заголовки форс-инклудом.
printf '#pragma once\n#include <windows.h>\n' > "$WORK/shims/Windows.h"
cat > "$WORK/shims/compat.h" <<'EOF'
#pragma once
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <thread>
#include <chrono>
EOF

export ZIG_LOCAL_CACHE_DIR="$WORK/zc-local"
export ZIG_GLOBAL_CACHE_DIR="$WORK/zc-global"

retry_on_cache() {
    local attempt
    for attempt in 1 2 3; do
        if "$@" 2>"$WORK/zig.log"; then return 0; fi
        if ! grep -q "CacheCheckFailed" "$WORK/zig.log"; then cat "$WORK/zig.log" >&2; return 1; fi
        echo "[i] CacheCheckFailed — чищу кэш, повтор..."
        rm -rf "$ZIG_LOCAL_CACHE_DIR" "$ZIG_GLOBAL_CACHE_DIR"
    done
    cat "$WORK/zig.log" >&2
    return 1
}

cd "$REPO/$SRC"

CXXFLAGS=(-target x86_64-windows-gnu -std=c++20 -O2 -fms-extensions
    -DNOMINMAX -DNDEBUG -DWIN32 -Dquintcs2_EXPORTS -D_WINDOWS -D_USRDLL
    -Wno-error=date-time
    -include "$WORK/shims/compat.h"
    -I"$WORK/shims" -Isrc -Iexternal/imgui -Iexternal/freetype/include -Iexternal -Iexternal/minhook)
CFLAGS=(-target x86_64-windows-gnu -O2 -Iexternal/minhook)

i=0
while IFS= read -r src; do
    i=$((i + 1))
    obj="$WORK/objs/obj_$i.o"
    [[ -f "$obj" ]] && continue
    s="${src//\\//}"
    if [[ "$s" == *.c || "$s" == *lz4.h ]]; then
        retry_on_cache "$ZIG" cc "${CFLAGS[@]}" -x c -c "$s" -o "$obj"
    else
        retry_on_cache "$ZIG" c++ "${CXXFLAGS[@]}" -c "$s" -o "$obj"
    fi
done < <(grep -oE 'ClCompile Include="[^"]*"' quint.vcxproj | sed 's/ClCompile Include="//;s/"$//')
echo "[*] объектов: $(ls "$WORK/objs" | wc -l)"

# CRT-шим для MSVC-либ (freetype/lz4) + setjmp на asm + стабы.
cat > "$WORK/crt_shim.cpp" <<'EOF'
extern "C" {
    unsigned long long __security_cookie = 0x00002B992DDFA232ULL;
    void __security_check_cookie(unsigned long long) {}
    void __report_rangecheckfailure() {}
    void __GSHandlerCheck() {}
}
EOF
cat > "$WORK/setjmp_shim.s" <<'EOF'
    .text
    .globl _setjmp
    .globl _longjmp
_setjmp:
    movq %rsp, (%rcx)
    movq (%rsp), %rax
    movq %rax, 80(%rcx)
    xorl %eax, %eax
    ret
_longjmp:
    movq (%rcx), %rsp
    movl %edx, %eax
    testl %eax, %eax
    jne 1f
    movl $1, %eax
1:  jmp *80(%rcx)
EOF
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O1 -c "$WORK/crt_shim.cpp" -o "$WORK/crt_shim.o"
retry_on_cache "$ZIG" cc -target x86_64-windows-gnu -c "$WORK/setjmp_shim.s" -o "$WORK/setjmp_shim.o"

cat > "$WORK/d3dcompiler.def" <<'DEF'
LIBRARY d3dcompiler_47.dll
EXPORTS
    D3DCompile
DEF
"$ZIG" dlltool -m i386:x86-64 -d "$WORK/d3dcompiler.def" -l "$WORK/stublibs/libd3dcompiler.a"
for st in libcmt LIBCMT oldnames OLDNAMES msvcrt MSVCRT; do
    cat > "$WORK/stub.def" <<EOF
LIBRARY $st.dll
EXPORTS
    _quint_stub_$st
EOF
    "$ZIG" dlltool -m i386:x86-64 -d "$WORK/stub.def" -l "$WORK/stublibs/lib$st.a"
done

retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O2 -shared \
    -o "$WORK/quint.dll" "$WORK"/objs/*.o "$WORK/crt_shim.o" "$WORK/setjmp_shim.o" \
    -ld3d11 -ldxgi -ldwmapi -limm32 -luser32 -lgdi32 -lwinmm -lntdll \
    -lkernel32 -lshell32 -lole32 \
    -L"$WORK/stublibs" -ld3dcompiler \
    external/freetype/win64/freetype.lib external/lz4.lib

# Инжектор NEVERWIN, дефолт — quint_vN.dll
cat > "$WORK/wmain_bridge.cpp" <<'CPP'
#include <windows.h>
#include <shellapi.h>
extern "C" int wmain(int argc, wchar_t* argv[]);
extern "C" int main(int, char**) {
    int argc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const int rc = wmain(argc, wargv);
    LocalFree(wargv);
    return rc;
}
CPP
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 \
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS \
    "-DINJECTOR_DEFAULT_DLL=\"quint_v${V}.dll\"" \
    -c "$REPO/neverwin-internal/injector/injector.cpp" -o "$WORK/injector.o"
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 \
    -c "$WORK/wmain_bridge.cpp" -o "$WORK/bridge.o"
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O2 \
    -o "$WORK/quint_injector.exe" "$WORK/injector.o" "$WORK/bridge.o" \
    -luser32 -lkernel32 -lshell32

mkdir -p "$REPO/$OUT"
cp "$WORK/quint.dll"          "$REPO/$OUT/quint_v${V}.dll"
cp "$WORK/quint_injector.exe" "$REPO/$OUT/quint_injector_v${V}.exe"

echo ""
echo "Готово:"
echo "  $OUT/quint_v${V}.dll"
echo "  $OUT/quint_injector_v${V}.exe"
