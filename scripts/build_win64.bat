@echo off
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Debug

set SCRIPT_DIR=%~dp0
set ROOT=%SCRIPT_DIR%..
cd /d "%ROOT%"

set PRESET=win64-debug
if /I "%CONFIG%"=="Release" set PRESET=win64-release

echo [repsp] Configuring (%PRESET%)...
cmake --preset %PRESET% -S "%ROOT%"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo [repsp] Building (%CONFIG%)...
cmake --build "%ROOT%\build\%PRESET%" --config %CONFIG%
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo [repsp] Running tests (%CONFIG%)...
ctest --test-dir "%ROOT%\build\%PRESET%" -C %CONFIG% --output-on-failure
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo [repsp] Win64 build and tests succeeded.
