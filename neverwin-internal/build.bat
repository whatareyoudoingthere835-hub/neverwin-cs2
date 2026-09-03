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
echo.
echo Загрузка: любой инжектор/лоадер (LoadLibraryW) в cs2.exe,
echo либо: ваш инжектор  <PID>  build\Release\neverwin.dll
