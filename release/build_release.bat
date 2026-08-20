@echo off
setlocal
rem ============================================================================
rem Сборка NEVERWIN release vN (Windows, Visual Studio 2022 + CMake).
rem
rem Использование:
rem   build_release.bat        — версия = автоинкремент (max(vN)+1)
rem   build_release.bat 2      — явная версия 2
rem
rem Результат:
rem   release\neverwin_vN.dll
rem   release\neverwin_injector_vN.exe
rem   neverwin.ini копируется рядом с DLL, если уже лежит в release\
rem ============================================================================
cd /d "%~dp0"

where cmake >nul 2>nul
if errorlevel 1 (
    echo [x] cmake не найден в PATH.
    exit /b 1
)

rem --- версия: аргумент или автоинкремент ---
set "V=%~1"
if "%V%"=="" (
    set "V=0"
    for %%f in (release\neverwin_v*.dll) do (
        set "name=%%~nf"
        call :parsever
    )
    set /a V=V+1
)
echo [*] Собираю версию v%V%

cmake -S neverwin-internal -B release\_build -A x64 -DCMAKE_BUILD_TYPE=Release -DNW_VERSION=%V%
if errorlevel 1 exit /b 1
cmake --build release\_build --config Release
if errorlevel 1 exit /b 1

if not exist release mkdir release
copy /y release\_build\Release\neverwin.dll          release\neverwin_v%V%.dll          >nul
copy /y release\_build\Release\neverwin_injector.exe release\neverwin_injector_v%V%.exe >nul
copy /y release\_build\Release\neverwin_overlay.exe  release\neverwin_overlay_v%V%.exe  >nul

if exist release\neverwin.ini (
    echo [i] neverwin.ini лежит рядом с DLL — оффсеты подхватятся.
) else (
    echo [!] neverwin.ini в release\ нет: DLL будет на встроенных оффсетах.
    echo     Сгенерируй: python neverwin-internal\tools\dump_to_ini.py ^<папка cs2-dumper output^> release\neverwin.ini
)

echo.
echo Готово:
echo   release\neverwin_v%V%.dll
echo   release\neverwin_injector_v%V%.exe
echo   release\neverwin_overlay_v%V%.exe
exit /b 0

:parsever
rem из neverwin_vN.dll вынимаем N и держим максимум
set "n=%name:neverwin_v=%"
set /a n=n+0
if %n% GEQ %V% set "V=%n%"
exit /b 0
