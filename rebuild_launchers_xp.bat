@echo off
REM Rebuild only the four launcher binaries after PC\launcher2.c changes.
REM Same toolchain as build_python_xp.bat step 3.7: v141_xp + SDK 7.1A
REM headers + UCRT 10240 + /MT + UseEnv=true + Py_XP. A for-loop over the
REM four projects trips over the ^ continuations under cmd.exe, so each
REM project gets an explicit block.
setlocal

if not defined VSROOT17 set "VSROOT17=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional"
set "MSBUILD17=%VSROOT17%\MSBuild\15.0\Bin\MSBuild.exe"
set "VCTOOLS17=%VSROOT17%\VC\Tools\MSVC\14.16.27023"
if not defined SDK71A set "SDK71A=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A"
set "SDK71A_EXTRA=%~dp0Tools\xp\sdk71a-extra"
if not defined SDK10 set "SDK10=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "UCRTVER=10.0.10240.0"
set "SRC=%~dp0."
set "EXT=%~dp0externals"
set "OBJDIR=%~dp0build\out\obj"
set "PCBUILD=%~dp0build\out\pcbuild"
set "OUT=%~dp0build\out\pcbuild\win32"
REM Build-assist interpreter; PYEXE overrides it (see README.md).
if not defined PYEXE (set "PYTHON=py") else set "PYTHON=%PYEXE%"

set "INCLUDE=%VCTOOLS17%\include;%SDK10%\Include\%UCRTVER%\ucrt;%SDK71A%\Include;%SDK71A_EXTRA%"
set "LIB=%VCTOOLS17%\lib\x86;%SDK10%\Lib\%UCRTVER%\ucrt\x86;%SDK71A%\Lib"
set "LIB32=%LIB%"
set "PATH=%VCTOOLS17%\bin\Hostx64\x86;%VCTOOLS17%\bin\Hostx64\x64;%VSROOT17%\Common7\IDE;%SDK71A%\Bin;%PATH%"
set "WindowsSDKDir_71A=%SDK71A%\"

echo [rebuild-launchers] pylauncher...
"%MSBUILD17%" "%SRC%\PCbuild\pylauncher.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT% ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [rebuild-launchers] pylauncher FAILED & exit /b 1)

echo [rebuild-launchers] pywlauncher...
"%MSBUILD17%" "%SRC%\PCbuild\pywlauncher.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT% ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [rebuild-launchers] pywlauncher FAILED & exit /b 1)

echo [rebuild-launchers] venvlauncher...
"%MSBUILD17%" "%SRC%\PCbuild\venvlauncher.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT% ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [rebuild-launchers] venvlauncher FAILED & exit /b 1)

echo [rebuild-launchers] venvwlauncher...
"%MSBUILD17%" "%SRC%\PCbuild\venvwlauncher.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT% ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [rebuild-launchers] venvwlauncher FAILED & exit /b 1)

echo [rebuild-launchers] all four launchers OK
endlocal
