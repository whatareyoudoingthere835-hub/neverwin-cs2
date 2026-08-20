cmake_minimum_required(VERSION 3.16)
project(neverwin_internal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Статический рантайм (/MT) — DLL не будет зависеть от vcruntime140.dll
# на целевой машине. Это одна из частых причин "вылета при инжекте".
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)

# ---------------------------------------------------------------------------
# DLL
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
)
target_include_directories(neverwin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_compile_definitions(neverwin PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)
# gdi32/dwmapi/d3dcompiler нужны бэкендам ImGui. В MSVC они подхватились бы
# через #pragma comment(lib, ...), но MinGW эти прагмы игнорирует — поэтому
# явно указываем для обоих компиляторов.
target_link_libraries(neverwin PRIVATE d3d11 dxgi d3dcompiler dwmapi imm32 user32 gdi32)

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
# Флаги по компиляторам
# ---------------------------------------------------------------------------
if(MSVC)
    target_compile_options(neverwin PRIVATE /W4 /utf-8)
    target_compile_options(neverwin_injector PRIVATE /W4 /utf-8)
endif()

if(MINGW)
    target_compile_options(neverwin PRIVATE -Wall -Wextra)
    target_link_options(neverwin PRIVATE -static -static-libgcc -static-libstdc++)

    target_compile_options(neverwin_injector PRIVATE -Wall -Wextra)
    # -municode: wmain() как точка входа в MinGW
    target_link_options(neverwin_injector PRIVATE -static -static-libgcc -static-libstdc++ -municode)
endif()
