@echo off
cd /d "%~dp0"
git pull
if errorlevel 1 exit /b 1

cmake -S . -B build -G "MinGW Makefiles"
if errorlevel 1 exit /b 1

cmake --build build
if errorlevel 1 exit /b 1

echo === Running ===
build\hello.exe