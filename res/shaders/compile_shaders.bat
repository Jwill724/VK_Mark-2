@echo off
cd /d "%~dp0"
setlocal

set VULKAN_BIN=%VULKAN_SDK%\Bin
set INCLUDE_PATH=-Ires/shaders/

if not exist "%VULKAN_BIN%\glslangValidator.exe" (
    echo Error: glslangValidator.exe not found in %VULKAN_BIN%!
    exit /b 1
)

echo Compiling GLSL shaders ...
for %%F in (*.vert *.frag *.comp) do (
    echo Compiling %%F ...
    "%VULKAN_BIN%\glslangValidator.exe" -V %INCLUDE_PATH% -o "%%~nF.spv" "%%F"
)

echo All shaders have been compiled to SPIR-V!
pause