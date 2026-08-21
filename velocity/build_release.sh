#!/usr/bin/env bash
# ============================================================================
# Сборка VELOCITY release vN (Linux, кросс-компиляция через zig + mingw-w64).
#
# Требуется zig 0.15+ в PATH (или переменная ZIG). На Linux без zig:
#   python3 -m pip install --user ziglang
#   export ZIG=$(python3 -c "import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),'zig'))")
#
# Использование:
#   ./build_release.sh          # версия = автоинкремент (max(vN)+1)
#   ./build_release.sh 2        # явная версия
#   ./build_release.sh --clean  # пересобрать все объекты с нуля
#
# Результат:
#   release/velocity_vN.dll
#   release/velocity_injector_vN.exe   (инжектор NEVERWIN, дефолт — velocity_vN.dll)
#
# Источник: velocity/ (velocity-main.zip от passtuh, как есть).
# Сборка с define DEV — protection-слой без VMProtect-виртуализации.
#
# Объекты кэшируются в velocity/.build/obj_N.o — повторная сборка быстрая.
# Менял исходники velocity — снеси velocity/.build (или --clean).
#
# ПАТЧИ ПОД СБОРКУ (только в рабочей копии, исходник velocity/ не тронут):
#  - phnt: enum-форварды получают тип : int; конфликты с mingw-заголовками
#    выключаются (#if 0); шимы minidumpapiset.h/ntlsa.h/sal.h — build_fixes/;
#  - stackwalker: guard для clang, strncpy_s -> strncpy;
#  - ShlObj.h -> shlobj.h; std::ranges::contains -> std::find;
#  - (void*)-касты указателей на функции и в hook-таблицах cheat/utility/vac;
#  - inline-syscall: inline в стабы (COMDAT-дубликаты);
#  - freetype.lib собран MSVC /MT: шим CRT (crt_shim.cpp) + минимальные
#    _setjmp/_longjmp на ассемблере (setjmp_shim.s) + стабы libcmt/oldnames;
#  - VMProtect: vmp_shim.cpp (no-op API) + стаб libVMProtectSDK64.a.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
REPO="$PWD"

SRC=velocity
OUT=release
ZIG="${ZIG:-zig}"
V="${1:-}"
if [[ "$V" == "--clean" ]]; then
    rm -rf "$SRC/.build"
    V=""
fi

if [[ -z "$V" ]]; then
    V=1
    for f in "$OUT"/velocity_v*.dll; do
        [[ -e "$f" ]] || continue
        n="${f##*velocity_v}"; n="${n%%.dll}"
        if (( n >= V )); then V=$((n + 1)); fi
    done
fi
[[ "$V" =~ ^[0-9]+$ ]] || { echo "версия должна быть числом"; exit 2; }

WORK="$REPO/$SRC/.build"
mkdir -p "$WORK/objs" "$WORK/stublibs" "$WORK/fixes"
rm -rf "$WORK/project" "$WORK/fixes"
mkdir -p "$WORK/fixes"
cp -r "$SRC/project" "$WORK/project"
cp -r "$SRC/build_fixes/." "$WORK/fixes/"

# Свежие кэши zig.
export ZIG_LOCAL_CACHE_DIR="$WORK/zcache-local"
export ZIG_GLOBAL_CACHE_DIR="$WORK/zcache-global"

retry_on_cache() {
    local attempt
    for attempt in 1 2 3; do
        if "$@" 2>"$WORK/zig.log"; then return 0; fi
        if ! grep -q "CacheCheckFailed" "$WORK/zig.log"; then cat "$WORK/zig.log" >&2; return 1; fi
        echo "[i] CacheCheckFailed (попытка $attempt/3) — чищу кэш, повторяю..."
        rm -rf "$ZIG_LOCAL_CACHE_DIR" "$ZIG_GLOBAL_CACHE_DIR"
    done
    cat "$WORK/zig.log" >&2
    return 1
}

P="$WORK/project"
F="$WORK/fixes"
cd "$P"

# --- патчи рабочей копии (см. шапку) ---
sed -i 's/typedef enum _\([A-Za-z0-9_]*\)/typedef enum _\1 : int/g' external/phnt/*.h
sed -i 's/= 0x80000000/= (int)0x80000000/' external/phnt/ntpsapi.h
sed -i 's/namespace protection::addresses { struct address_t; }/namespace protection::addresses { union address_t; }/' protection/patterns.hpp
sed -i 's/#if defined(_MSC_VER)$/#if defined(_MSC_VER) || defined(__clang__)/' external/stackwalker/stackwalker.hpp
sed -i 's/#if _MSC_VER < 1300/#if defined(_MSC_VER) \&\& (_MSC_VER < 1300)/' external/stackwalker/stackwalker.hpp
sed -i 's/strncpy_s( szDest, nMaxDestSize, szSrc, _TRUNCATE );/strncpy( szDest, szSrc, nMaxDestSize - 1 );/' external/stackwalker/stackwalker.cpp
sed -i 's/#include <ShlObj.h>/#include <shlobj.h>/' core/features/misc/impl/impacts.cpp
sed -i 's/std::ranges::contains (cloud_materials, material_hash)/std::find(cloud_materials.begin(), cloud_materials.end(), material_hash) != cloud_materials.end()/;s/std::ranges::contains (sun_materials, material_hash)/std::find(sun_materials.begin(), sun_materials.end(), material_hash) != sun_materials.end()/' core/features/world/impl/scene.cpp
sed -i 's/&m_\([a-z0-9_]*\), &\([a-z0-9_]*\)/\&m_\1, (void*)\&\2/g' core/hooks/impl/cheat.cpp core/hooks/impl/utility.cpp core/hooks/impl/vac.cpp
sed -i 's/, &wnd_proc )/, (void*)\&wnd_proc )/;s/, &om_set_render_targets )/, (void*)\&om_set_render_targets )/;s/, &render_smoke_map )/, (void*)\&render_smoke_map )/;s/, &render_smoke_unmap )/, (void*)\&render_smoke_unmap )/' core/hooks/impl/cheat.cpp
sed -i 's/JM_INLINE_SYSCALL_FORCEINLINE std::int32_t syscall(/JM_INLINE_SYSCALL_FORCEINLINE inline std::int32_t syscall(/' external/inline-syscall/inline_syscall.inl
python3 "$F/patch_phnt_conflict.py" external/phnt/ntrtl.h RtlSetHeapInformation || true
python3 "$F/patch_phnt_conflict.py" external/phnt/ntrtl.h RtlMultipleAllocateHeap || true
python3 "$F/patch_phnt_conflict.py" external/phnt/ntrtl.h RtlMultipleFreeHeap || true

CXXFLAGS=(-target x86_64-windows-gnu -std=c++20 -O2 -fms-extensions -mavx2 -mfma
    -DVELOCITYCS2_EXPORTS -D_WINDOWS -D_USRDLL -DDEV -DNDEBUG
    -Wno-error=date-time
    -include "$F/phnt_compat.hpp"
    -I"$F" -I. -Iexternal/phnt -Iexternal/vmprotect
    -Iexternal/xdraw/dependencies/freetype/x)
CFLAGS=(-target x86_64-windows-gnu -O2 -DVELOCITYCS2_EXPORTS -D_WINDOWS -D_USRDLL -DDEV -DNDEBUG
    -I. -Iexternal/phnt)

# --- компиляция (объекты кэшируются в velocity/.build/objs) ---
i=0
while IFS= read -r src; do
    i=$((i + 1))
    obj="$WORK/objs/obj_$i.o"
    [[ -f "$obj" ]] && continue
    if [[ "$src" == *.c ]]; then
        retry_on_cache "$ZIG" cc "${CFLAGS[@]}" -c "$src" -o "$obj"
    else
        retry_on_cache "$ZIG" c++ "${CXXFLAGS[@]}" -c "$src" -o "$obj"
    fi
done < <(grep -o 'ClCompile Include="[^"]*"' "$REPO/$SRC/velocity-cs2.vcxproj" | sed 's/ClCompile Include="//;s/"$//' | sed 's/\\/\//g' | sed "s|^project/||")
echo "[*] объектов: $(ls "$WORK/objs" | wc -l)"

# --- шимы и стабы (freetype MSVC-CRT, VMProtect, d3dcompiler) ---
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
cat > "$WORK/vmp_shim.cpp" <<'EOF'
extern "C" {
__declspec(dllexport) void VMProtectBegin(const char*) {}
__declspec(dllexport) void VMProtectBeginVirtualization(const char*) {}
__declspec(dllexport) void VMProtectBeginMutation(const char*) {}
__declspec(dllexport) void VMProtectBeginUltra(const char*) {}
__declspec(dllexport) void VMProtectEnd() {}
__declspec(dllexport) int VMProtectIsProtected() { return 0; }
__declspec(dllexport) int VMProtectIsValidImageCRC() { return 1; }
__declspec(dllexport) char* VMProtectDecryptStringA(const char* s) { return (char*)s; }
__declspec(dllexport) wchar_t* VMProtectDecryptStringW(const wchar_t* s) { return (wchar_t*)s; }
}
EOF
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O1 -c "$WORK/crt_shim.cpp" -o "$WORK/crt_shim.o"
retry_on_cache "$ZIG" cc -target x86_64-windows-gnu -c "$WORK/setjmp_shim.s" -o "$WORK/setjmp_shim.o"
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O1 -c "$WORK/vmp_shim.cpp" -o "$WORK/vmp_shim.o"

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
    _neverwin_stub_$st
EOF
    "$ZIG" dlltool -m i386:x86-64 -d "$WORK/stub.def" -l "$WORK/stublibs/lib$st.a"
done
cat > "$WORK/vmp.def" <<'DEF'
LIBRARY VMProtectSDK64.dll
EXPORTS
    VMProtectBegin
    VMProtectBeginVirtualization
    VMProtectBeginMutation
    VMProtectBeginUltra
    VMProtectEnd
    VMProtectIsProtected
    VMProtectIsValidImageCRC
    VMProtectDecryptStringA
    VMProtectDecryptStringW
DEF
"$ZIG" dlltool -m i386:x86-64 -d "$WORK/vmp.def" -l "$WORK/stublibs/libVMProtectSDK64.a"

# --- линковка DLL ---
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O2 -shared \
    -o "$WORK/velocity.dll" "$WORK"/objs/*.o \
    "$WORK/vmp_shim.o" "$WORK/crt_shim.o" "$WORK/setjmp_shim.o" \
    -ld3d11 -ldxgi -ldwmapi -limm32 -luser32 -lgdi32 \
    -lkernel32 -lshell32 -lole32 -ldbghelp -lversion -lwinhttp \
    -lapi-ms-win-core-synch-l1-2-0 \
    -L"$WORK/stublibs" -ld3dcompiler \
    external/xdraw/dependencies/freetype/x/freetype.lib

# --- инжектор (наш, дефолт — velocity_vN.dll) ---
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
    "-DINJECTOR_DEFAULT_DLL=\"velocity_v${V}.dll\"" \
    -c "$REPO/neverwin-internal/injector/injector.cpp" -o "$WORK/injector.o"
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -std=c++17 -O2 \
    -c "$WORK/wmain_bridge.cpp" -o "$WORK/bridge.o"
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O2 \
    -o "$WORK/velocity_injector.exe" "$WORK/injector.o" "$WORK/bridge.o" \
    -luser32 -lkernel32 -lshell32

# --- раскладка по release/ ---
mkdir -p "$OUT"
cp "$WORK/velocity.dll"          "$OUT/velocity_v${V}.dll"
cp "$WORK/velocity_injector.exe" "$OUT/velocity_injector_v${V}.exe"

echo ""
echo "Готово:"
echo "  $OUT/velocity_v${V}.dll"
echo "  $OUT/velocity_injector_v${V}.exe"
echo ""
echo "[!] ВАЖНО: оффсеты/сигнатуры внутри velocity — от их старого билда."
echo "    На текущем клиенте CS2 DLL может не подняться — это исходник как есть."
echo "[!] DEV-сборка: VMProtect-защита не активна."
