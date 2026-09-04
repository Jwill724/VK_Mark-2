@echo off
setlocal

cd /d "%~dp0"

echo.
echo ========================================
echo Building NVIDIA NRD
echo ========================================
echo.

where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake was not found in PATH.
    exit /b 1
)

where git >nul 2>&1
if errorlevel 1 (
    echo ERROR: Git was not found in PATH.
    exit /b 1
)

echo CMake:
cmake --version
echo.

REM ==========================================================
REM Configure
REM ==========================================================

echo Configuring NRD...
echo.

cmake ^
    -S "%~dp0cmake\nrd" ^
    -B "%~dp0build\nrd" ^
    -G "Visual Studio 18 2026" ^
    -A x64

if errorlevel 1 (
    echo.
    echo ERROR: NRD configuration failed.
    exit /b 1
)

REM ==========================================================
REM Debug
REM ==========================================================

echo.
echo ========================================
echo Building NRD Debug
echo ========================================
echo.

cmake --build "%~dp0build\nrd" ^
    --config Debug ^
    --target nrd_vendor ^
    --parallel

if errorlevel 1 (
    echo.
    echo ERROR: NRD Debug build failed.
    exit /b 1
)

REM ==========================================================
REM Release
REM ==========================================================

echo.
echo ========================================
echo Building NRD Release
echo ========================================
echo.

cmake --build "%~dp0build\nrd" ^
    --config Release ^
    --target nrd_vendor ^
    --parallel

if errorlevel 1 (
    echo.
    echo ERROR: NRD Release build failed.
    exit /b 1
)

REM ==========================================================
REM Sync headers
REM
REM vendor\nrd\Include is what the Visual Studio project
REM compiles against. Copy it from the fetched source tree so
REM it can never drift from the libraries built above.
REM ==========================================================

echo.
echo ========================================
echo Syncing NRD headers
echo ========================================
echo.

if not exist "%~dp0build\nrd\_deps\nrdsdk-src\Include" (
    echo ERROR: NRD source headers not found at:
    echo   %~dp0build\nrd\_deps\nrdsdk-src\Include
    echo.
    echo Check the FetchContent name in cmake\nrd\CMakeLists.txt
    echo and list %~dp0build\nrd\_deps to find the real path.
    exit /b 1
)

xcopy /Y /E /I ^
    "%~dp0build\nrd\_deps\nrdsdk-src\Include" ^
    "%~dp0vendor\nrd\Include"

if errorlevel 1 (
    echo.
    echo ERROR: NRD header copy failed.
    exit /b 1
)

REM ==========================================================
REM Complete
REM ==========================================================

echo.
echo ========================================
echo NRD build complete
echo ========================================
echo.
echo Libraries:
echo.
echo   vendor\nrd\Lib\Debug\NRD.lib
echo   vendor\nrd\Lib\Debug\ShaderMakeBlob.lib
echo.
echo   vendor\nrd\Lib\Release\NRD.lib
echo   vendor\nrd\Lib\Release\ShaderMakeBlob.lib
echo.
echo Headers:
echo.
echo   vendor\nrd\Include
echo.

endlocal