cmake_minimum_required(VERSION 3.16)
project(neverwin_internal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Версия релиза (vN): показывается в меню и уходит в shared memory оверлея.
# Передаётся из release/build_release.bat: -DNW_VERSION=%V%
set(NW_VERSION 0 CACHE STRING "Версия релиза (vN)")

# Статический рантайм (/MT) — DLL не будет зависеть от vcruntime140.dll
# на целевой машине. Это одна из частых причин "вылета при инжекте".
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
set(MINHOOK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/minhook)

# ---------------------------------------------------------------------------
# DLL
# Меню рисуется В ИГРЕ по схеме quintcs2: реальный свопчейн игры из
# rendersystemdx11.dll (сигнатура), MinHook на Present/ResizeBuffers,
# InputSystem::IsRelativeMouseMode, WndProc-хук. Никаких dummy-свопчейнов.
# Не встал рендер (Vulkan) — меню берёт на себя внешний оверлей.
# ---------------------------------------------------------------------------
add_library(neverwin SHARED
    src/main.cpp
    src/features.cpp
    src/gui.cpp
    src/offsets.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${MINHOOK_DIR}/buffer.c
    ${MINHOOK_DIR}/hook.c
    ${MINHOOK_DIR}/trampoline.c
    ${MINHOOK_DIR}/hde/hde32.c
    ${MINHOOK_DIR}/hde/hde64.c
)
target_include_directories(neverwin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${MINHOOK_DIR}
)
target_compile_definitions(neverwin PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    NW_VERSION=${NW_VERSION}
)
# d3d11/dxgi/d3dcompiler нужны ImGui-бэкенду, dwmapi/imm32 — win32-бэкенду.
# В MSVC они подхватились бы через #pragma comment(lib, ...), MinGW прагмы
# игнорирует — указываем явно для обоих.
target_link_libraries(neverwin PRIVATE d3d11 dxgi d3dcompiler dwmapi imm32 user32 gdi32)

# ---------------------------------------------------------------------------
# Флаги по компиляторам
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(neverwin PRIVATE /W4 /utf-8)
endif()

if(MINGW)
    target_compile_options(neverwin PRIVATE -Wall -Wextra)
    target_link_options(neverwin PRIVATE -static -static-libgcc -static-libstdc++)
endif()
