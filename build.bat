@echo off
REM ---------------------------------------------------------------------------
REM  Smart Traffic Flow - one-click build, test and run (Windows)
REM  Uses the portable toolchain in tools\ if present (GCC 13.1, CMake, SFML 2.6.1),
REM  otherwise whatever g++ / cmake / SFML are on your PATH.
REM ---------------------------------------------------------------------------
setlocal
cd /d "%~dp0"

if exist "tools\mingw64\bin\g++.exe" set "PATH=%~dp0tools\mingw64\bin;%PATH%"
if exist "tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe" set "PATH=%~dp0tools\cmake-3.30.5-windows-x86_64\bin;%PATH%"

cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release || goto :error
cmake --build build -j 8 || goto :error

echo.
echo ===== Running unit tests =====
build\engine_tests.exe || goto :error

if /I "%1"=="--no-run" goto :eof
echo.
echo ===== Launching the game =====
start "" /D build build\SmartTrafficFlow.exe
goto :eof

:error
echo.
echo Build failed. See the messages above.
exit /b 1
