@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [x] cmake не найден в PATH. Поставь CMake или открой эту папку в Visual Studio 2022.
    exit /b 1
)

cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo Готово:
echo   DLL:      build\Release\neverwin.dll
echo   Инжектор: build\Release\neverwin_injector.exe
echo.
echo Инжект: neverwin_injector.exe   (или: neverwin_injector.exe ^<PID^> ^<путь к dll^>)
