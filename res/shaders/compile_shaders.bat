@echo off
cd /d "%~dp0"
setlocal enabledelayedexpansion

set VULKAN_BIN=%VULKAN_SDK%\Bin
set IGNORE_DIRS=deprecated

set INCLUDE_PATH=^
 -Iinclude/^
 -Idebug/^
 -Ienvironment/^
 -Ireflections/^
 -Icore/^
 -Ishadows/^
 -Ipost_process/^
 -Issgi/^
 -Iclustered/^
 -Ivisibility/^
 -I../../vendor/nrd/Shaders/

if not exist "%VULKAN_BIN%\glslangValidator.exe" (
	echo Error: glslangValidator.exe not found in %VULKAN_BIN%!
	exit /b 1
)

set /a FAILED=0

for /R %%F in (*.vert *.frag *.comp *.task *.mesh) do (
	set "SKIP="
	set "FDIR=%%~dpF"
	for %%D in (%IGNORE_DIRS%) do if not "!FDIR:\%%D\=!"=="!FDIR!" set "SKIP=1"

	if defined SKIP (
		echo Skipping %%F
	) else (
		echo Compiling %%F ...

		if "%%~xF"==".vert" (
			"%VULKAN_BIN%\glslangValidator.exe" -V --target-env vulkan1.4 -g -S vert !INCLUDE_PATH! -o "%%~dpnF.spv" "%%F"
		) else if "%%~xF"==".frag" (
			"%VULKAN_BIN%\glslangValidator.exe" -V --target-env vulkan1.4 -g -S frag !INCLUDE_PATH! -o "%%~dpnF.spv" "%%F"
		) else if "%%~xF"==".comp" (
			"%VULKAN_BIN%\glslangValidator.exe" -V --target-env vulkan1.4 -g -S comp !INCLUDE_PATH! -o "%%~dpnF.spv" "%%F"
		) else if "%%~xF"==".task" (
			"%VULKAN_BIN%\glslangValidator.exe" -V --target-env vulkan1.4 -g -S task !INCLUDE_PATH! -o "%%~dpnF.spv" "%%F"
		) else if "%%~xF"==".mesh" (
			"%VULKAN_BIN%\glslangValidator.exe" -V --target-env vulkan1.4 -g -S mesh !INCLUDE_PATH! -o "%%~dpnF.spv" "%%F"
		)

		if ERRORLEVEL 1 (
			echo Failed to compile %%F
			set /a FAILED+=1
		)
	)
)

if %FAILED% GTR 0 (
	echo %FAILED% shader^(s^) failed to compile.
) else (
	echo All shaders have been compiled to SPIR-V!
)
pause
