@echo off
cd /d "%~dp0"
setlocal

set VULKAN_BIN=%VULKAN_SDK%\Bin
set INCLUDE_PATH=-Ires/shaders/

if not exist "%VULKAN_BIN%\glslangValidator.exe" (
    echo Error: glslangValidator.exe not found in %VULKAN_BIN%!
    exit /b 1
)

for %%F in (*.vert *.frag *.comp) do (
    echo Compiling %%F ...
    set FILE=%%~nxF

    if "%%~xF"==".vert" (
        "%VULKAN_BIN%\glslangValidator.exe" -V --target-env vulkan1.3 -S vert %INCLUDE_PATH% -o "%%~nF.spv" "%%F"
    ) else if "%%~xF"==".frag" (
        "%VULKAN_BIN%\glslangValidator.exe" -V --target-env vulkan1.3 -S frag %INCLUDE_PATH% -o "%%~nF.spv" "%%F"
    ) else if "%%~xF"==".comp" (
        "%VULKAN_BIN%\glslangValidator.exe" -V --target-env vulkan1.3 -S comp %INCLUDE_PATH% -o "%%~nF.spv" "%%F"
    )

    if ERRORLEVEL 1 (
        echo Failed to compile %%F
    )
)

echo All shaders have been compiled to SPIR-V!
pause