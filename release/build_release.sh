#!/usr/bin/env bash
# ============================================================================
# Сборка NEVERWIN release vN (Linux, кросс-компиляция через zig + mingw-w64).
#
# Требуется zig 0.15+ в PATH (или переменная ZIG с путём к бинарю).
# На Linux без zig:  python3 -m pip install --user ziglang
#                    export ZIG=$(python3 -c "import ziglang,os;print(os.path.join(os.path.dirname(ziglang.__file__),'zig'))")
#
# Использование:
#   ./build_release.sh          # версия = автоинкремент (max(vN)+1)
#   ./build_release.sh 2        # явная версия 2
#
# Результат:
#   release/neverwin_vN.dll           — DLL (x64, статический рантайм)
#   release/neverwin_injector_vN.exe  — инжектор
#   release/neverwin.ini              — если уже лежит в release/, копируется
#                                       рядом с DLL (DLL ищет ini рядом с собой)
#
# Примечание: каждый .cpp компилируется ОТДЕЛЬНОЙ инвокацией (-c), и только
# потом идёт линковка. Так ниже пиковая память (одна инвокация со всеми
# файлами разом тянет за собой компиляцию libc++ и на машинах с 4 ГБ может
# упасть), и ошибка компиляции сразу указывает на файл.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

SRC=neverwin-internal
OUT=release
ZIG="${ZIG:-zig}"

# --- версия ---
if [[ $# -ge 1 ]]; then
    V="$1"
else
    V=1
    for f in "$OUT"/neverwin_v*.dll; do
        [[ -e "$f" ]] || continue
        n="${f##*neverwin_v}"
        n="${n%%.dll}"
        if (( n >= V )); then V=$((n + 1)); fi
    done
fi
[[ "$V" =~ ^[0-9]+$ ]] || { echo "версия должна быть числом"; exit 2; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Свежие кэши на каждую сборку; на случай флаки CacheCheckFailed —
# retry_on_cache чистит их и повторяет.
ZCACHE_LOCAL="$WORK/zcache-local"
ZCACHE_GLOBAL="$WORK/zcache-global"
export ZIG_LOCAL_CACHE_DIR="$ZCACHE_LOCAL"
export ZIG_GLOBAL_CACHE_DIR="$ZCACHE_GLOBAL"

retry_on_cache() {
    local attempt
    for attempt in 1 2 3; do
        if "$@" 2>"$WORK/zig.log"; then
            return 0
        fi
        if ! grep -q "CacheCheckFailed" "$WORK/zig.log"; then
            cat "$WORK/zig.log" >&2
            return 1
        fi
        echo "[i] CacheCheckFailed (попытка $attempt/3) — чищу кэш, повторяю..."
        rm -rf "$ZCACHE_LOCAL" "$ZCACHE_GLOBAL"
    done
    cat "$WORK/zig.log" >&2
    return 1
}

COMMON=(-target x86_64-windows-gnu -std=c++17 -O2
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS
    -DNW_VERSION="$V"
    -I"$SRC/src" -I"$SRC/thirdparty/imgui" -I"$SRC/thirdparty/imgui/backends")

# --- import lib для d3dcompiler_47.dll ---
# В комплекте zig её нет, а imgui_impl_dx11.cpp зовёт D3DCompile().
cat > "$WORK/d3dcompiler.def" <<'DEF'
LIBRARY d3dcompiler_47.dll
EXPORTS
    D3DCompile
DEF
"$ZIG" dlltool -m i386:x86-64 -d "$WORK/d3dcompiler.def" -l "$WORK/libd3dcompiler.a"

# --- DLL: фаза 1 — каждый TU в свой .o ---
DLL_SRCS=(
    "$SRC/src/main.cpp" "$SRC/src/features.cpp"
    "$SRC/src/gui.cpp" "$SRC/src/offsets.cpp"
    "$SRC/thirdparty/imgui/imgui.cpp" "$SRC/thirdparty/imgui/imgui_draw.cpp"
    "$SRC/thirdparty/imgui/imgui_tables.cpp" "$SRC/thirdparty/imgui/imgui_widgets.cpp"
    "$SRC/thirdparty/imgui/backends/imgui_impl_win32.cpp"
    "$SRC/thirdparty/imgui/backends/imgui_impl_dx11.cpp"
)
OBJS=()
i=0
for src in "${DLL_SRCS[@]}"; do
    i=$((i + 1))
    obj="$WORK/obj_$i.o"
    retry_on_cache "$ZIG" c++ "${COMMON[@]}" -c "$src" -o "$obj"
    OBJS+=("$obj")
done

# --- DLL: фаза 2 — линковка ---
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O2 -shared \
    -o "$WORK/neverwin.dll" "${OBJS[@]}" \
    -ld3d11 -ldxgi -ldwmapi -limm32 -luser32 -lgdi32 -L"$WORK" -ld3dcompiler

# --- инжектор ---
# injector.cpp объявляет wmain(); crt от mingw зовёт main(), поэтому мост:
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
retry_on_cache "$ZIG" c++ "${COMMON[@]}" -c "$SRC/injector/injector.cpp" -o "$WORK/injector.o"
retry_on_cache "$ZIG" c++ "${COMMON[@]}" -c "$WORK/wmain_bridge.cpp" -o "$WORK/bridge.o"
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O2 \
    -o "$WORK/neverwin_injector.exe" "$WORK/injector.o" "$WORK/bridge.o" \
    -luser32 -lkernel32 -lshell32

# --- оверлей (внешний HUD, D2D) ---
retry_on_cache "$ZIG" c++ "${COMMON[@]}" -c "$SRC/overlay/overlay.cpp" -o "$WORK/overlay.o"
retry_on_cache "$ZIG" c++ -target x86_64-windows-gnu -O2 \
    -o "$WORK/neverwin_overlay.exe" "$WORK/overlay.o" "$WORK/bridge.o" \
    -ld2d1 -ldwrite -ld3d11 -ldxgi -luser32 -lkernel32

# --- раскладка по release/ ---
mkdir -p "$OUT"
cp "$WORK/neverwin.dll"          "$OUT/neverwin_v${V}.dll"
cp "$WORK/neverwin_injector.exe" "$OUT/neverwin_injector_v${V}.exe"
cp "$WORK/neverwin_overlay.exe"  "$OUT/neverwin_overlay_v${V}.exe"
if [[ -f "$OUT/neverwin.ini" ]]; then
    echo "[i] neverwin.ini лежит рядом с DLL — оффсеты подхватятся."
else
    echo "[!] neverwin.ini в release/ нет: DLL будет на встроенных оффсетах."
    echo "    Сгенерируй: python3 neverwin-internal/tools/dump_to_ini.py <папка cs2-dumper output> release/neverwin.ini"
fi

echo ""
echo "Готово:"
echo "  $OUT/neverwin_v${V}.dll"
echo "  $OUT/neverwin_injector_v${V}.exe"
echo "  $OUT/neverwin_overlay_v${V}.exe"
