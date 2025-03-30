@echo off
cd /d "%~dp0"
setlocal

set VULKAN_BIN=%VULKAN_SDK%\Bin

if not exist "%VULKAN_BIN%\dxc.exe" (
    echo Error: dxc.exe not found in %VULKAN_BIN%!
    exit /b 1
)

set TARGET_ENV=-fspv-target-env=vulkan1.3

echo Compiling vertex shaders...
for %%F in (*_vert.hlsl) do (
    echo %%F
    "%VULKAN_BIN%\dxc.exe" -spirv %TARGET_ENV% -T vs_6_0 -E main -Fo "%%~nF.spv" "%%F"
)

echo Compiling pixel/fragment shaders...
for %%F in (*_frag.hlsl) do (
    echo %%F
    "%VULKAN_BIN%\dxc.exe" -spirv %TARGET_ENV% -T ps_6_0 -E main -Fo "%%~nF.spv" "%%F"
)

echo Compiling compute shaders...
for %%F in (*_comp.hlsl) do (
    echo %%F
    "%VULKAN_BIN%\dxc.exe" -spirv %TARGET_ENV% -T cs_6_0 -E main -Fo "%%~nF.spv" "%%F"
)

echo All HLSL shaders have been compiled to SPIR-V!
pause