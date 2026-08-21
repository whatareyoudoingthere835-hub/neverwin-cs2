cmake_minimum_required(VERSION 3.16)
project(neverwin_internal LANGUAGES CXX)

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

# ---------------------------------------------------------------------------
# DLL
# Меню больше не рисуется внутри игры (хук Present убран — он ронял CS2 при
# инжекте). DLL: фичи + shared memory + хоткеи. Меню/HUD — внешний оверлей.
# ---------------------------------------------------------------------------
add_library(neverwin SHARED
    src/main.cpp
    src/features.cpp
    src/gui.cpp
    src/offsets.cpp
)
target_include_directories(neverwin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(neverwin PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    NW_VERSION=${NW_VERSION}
)
target_link_libraries(neverwin PRIVATE user32)

# ---------------------------------------------------------------------------
# Инжектор
# ---------------------------------------------------------------------------
add_executable(neverwin_injector injector/injector.cpp)
target_compile_definitions(neverwin_injector PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ---------------------------------------------------------------------------
# Внешний оверлей: меню + HUD на DirectX 12 (ImGui + imgui_impl_dx12).
# Не инжектится: читает shared memory, команды шлёт обратно в DLL.
# ---------------------------------------------------------------------------
add_executable(neverwin_overlay
    overlay/overlay.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx12.cpp
)
target_include_directories(neverwin_overlay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_compile_definitions(neverwin_overlay PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NW_VERSION=${NW_VERSION}
)
target_link_libraries(neverwin_overlay PRIVATE d3d12 dxgi d3dcompiler dwmapi imm32 user32 gdi32)

# ---------------------------------------------------------------------------
# Флаги по компиляторам
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(neverwin PRIVATE /W4 /utf-8)
    target_compile_options(neverwin_injector PRIVATE /W4 /utf-8)
    target_compile_options(neverwin_overlay PRIVATE /W4 /utf-8)
endif()

if(MINGW)
    target_compile_options(neverwin PRIVATE -Wall -Wextra)
    target_link_options(neverwin PRIVATE -static -static-libgcc -static-libstdc++)

    target_compile_options(neverwin_injector PRIVATE -Wall -Wextra)
    # -municode: wmain() как точка входа в MinGW
    target_link_options(neverwin_injector PRIVATE -static -static-libgcc -static-libstdc++ -municode)
endif()
