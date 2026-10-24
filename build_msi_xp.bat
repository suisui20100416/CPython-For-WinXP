@echo off
REM Build the XP Python MSI installer chain: sub-MSI packages then bundle.
setlocal
if not defined MSBUILD set "MSBUILD=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
set "MSI=%~dp0Tools\msi"
set "BUILD=%~dp0build"
set "LAYOUT=%~dp0build\out"
set "OBJDIR=%~dp0build\out\obj"
set "PCBUILD=%~dp0build\out\pcbuild"
set "OUT=%~dp0build\out\pcbuild\win32"
set "EXT=%~dp0externals"
set "WIX=%EXT%\windows-installer\wix-314"
set "SRC=%~dp0."

REM Build-assist Python.  The toolchain's default is "py -3.14" but that
REM interpreter is no longer installed on this host; the 3.15 runtime in
REM C:\Program Files\Python315 takes over.  msi.props runs PythonForBuild
REM through cmd without quoting, so use the space-free "py" launcher for
REM MSBuild and the full path only for our own steps.
set "PYAID=py"
if not defined PYEXE set "PYEXE=py"

REM BuildForRelease=true selects the clean "3.15.0" version branch in
REM msi.props; without it every sub-MSI stamps dev builds as
REM "3.15.0dev<days-since-2001>" in ProductName/ProductVersion, which is
REM what the ARP entries and the bundle progress UI then display.
set "COMMON=/nologo /v:m /p:Platform=x86 /p:BuildForRelease=true /p:Py_OutDir=%PCBUILD% /p:ExternalsDir=%EXT%\ /p:WixInstallPath=%WIX%\ /p:PySourcePath=%SRC%\ /p:PySourceDir=%SRC%\ /p:IncludeExternals=false /p:SkipCopySSLDLL=true /p:XPBuild=true /p:PythonForBuild=%PYAID%"

REM Stage pip runtime files for lib.msi.  pip.msi is an empty marker
REM package on XP: its WixCA custom action cannot run (WiX binary is
REM subsystem 6.0) and its registry KeyPath component fails component
REM registration on this target.  Shipping pip as ordinary files in
REM lib.msi avoids both failure points and needs no ensurepip at install.
REM The pip files are generated right here with the freshly built XP
REM interpreter (no layout-tree prerequisite), from the XP-patched wheel
REM vendored in Lib\ensurepip\_bundled.
REM The console launchers are .cmd wrappers using %~dp0 so they resolve
REM ..\python.exe at run time; ensurepip's distlib pip*.exe launchers
REM embed the build machine's absolute interpreter path and are removed.
echo [msi] step: generate pip runtime files via ensurepip
set "PYHOME_SAVE=%PYTHONHOME%"
set "PYTHONHOME=%SRC%"
if exist "%OUT%\pip-venv" rd /s /q "%OUT%\pip-venv"
if exist "%OUT%\Lib\site-packages\pip" rd /s /q "%OUT%\Lib\site-packages\pip"
for /d %%D in ("%OUT%\Lib\site-packages\pip-*.dist-info") do rd /s /q "%%~fD"
if not exist "%OUT%\Lib\site-packages" mkdir "%OUT%\Lib\site-packages"
if not exist "%OUT%\Scripts" mkdir "%OUT%\Scripts"
REM A venv of the freshly built interpreter self-bootstraps pip from the
REM XP-patched wheel vendored in Lib\ensurepip\_bundled; its site-packages
REM then holds exactly the files lib.msi ships (pip + dist-info).
set "PYTHONHOME=%SRC%"
REM The venv module copies its launchers from the stdlib's
REM Lib\venv\scripts\nt (in the installed layout lib.msi ships them
REM there). Seed that directory from the build output for this bootstrap
REM venv; *.exe is gitignored, so the working tree stays clean.
copy /y "%OUT%\venvlauncher.exe" "%SRC%\Lib\venv\scripts\nt\venvlauncher.exe" >nul
copy /y "%OUT%\venvwlauncher.exe" "%SRC%\Lib\venv\scripts\nt\venvwlauncher.exe" >nul
"%OUT%\python.exe" -m venv "%OUT%\pip-venv"
if errorlevel 1 (echo [msi] venv bootstrap FAILED & exit /b 1)
if not exist "%OUT%\pip-venv\Lib\site-packages\pip" (echo [msi] venv produced no pip package & exit /b 1)
robocopy "%OUT%\pip-venv\Lib\site-packages\pip" "%OUT%\Lib\site-packages\pip" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (echo [msi] stage pip package FAILED & exit /b 1)
for /d %%D in ("%OUT%\pip-venv\Lib\site-packages\pip-*.dist-info") do robocopy "%%~fD" "%OUT%\Lib\site-packages\%%~nxD" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (echo [msi] stage pip dist-info FAILED & exit /b 1)
rd /s /q "%OUT%\pip-venv"
REM Remove the seeded launchers again: lib.wixproj collects Lib\**\* from
REM the source tree, and a second copy of the launcher files under
REM Lib\venv\scripts\nt would collide with the explicit DLLs-directory
REM components (identical target path -> identical auto GUID).
del "%SRC%\Lib\venv\scripts\nt\venvlauncher.exe" "%SRC%\Lib\venv\scripts\nt\venvwlauncher.exe" 2>nul
set "PYTHONHOME=%PYHOME_SAVE%"
del /q "%OUT%\Scripts\pip.exe" "%OUT%\Scripts\pip3.exe" "%OUT%\Scripts\pip3.15.exe" 2>nul
copy /y "%~dp0Tools\xp\pip_launcher.cmd" "%OUT%\Scripts\pip.cmd" >nul
copy /y "%~dp0Tools\xp\pip_launcher.cmd" "%OUT%\Scripts\pip3.cmd" >nul
copy /y "%~dp0Tools\xp\pip_launcher.cmd" "%OUT%\Scripts\pip3.15.cmd" >nul
type nul > "%OUT%\Scripts\.pip-launcher"

REM pip's platformdirs SHGetKnownFolderPath fallback ships inside the
REM vendored wheel (Lib\ensurepip\_bundled, commit 84a41d1910e) - the venv
REM bootstrap above installs from that wheel, so the staged tree and the
REM source tree are already patched.  No runtime patching needed anymore
REM (18-test; the two patch scripts were retired).

REM Force a clean MSI build every round. The _TransformWxlTemplates target
REM stamps {{LongVersion}} (= PythonVersion) into generated .wxl files in
REM these intermediates, but its incremental Inputs are only the template
REM and patchlevel.h, so a changed PythonVersion never regenerates them;
REM candle/light likewise treat dev-versioned outputs as up-to-date. Wipe
REM all msi_* intermediates so ProductName/ProductVersion are always built
REM from the current properties.
for /d %%D in ("%SRC%\PCbuild\obj\315win32_Release\msi_*") do rd /s /q "%%D"

for %%P in (launcher core exe lib pip path appendpath dev) do (
  echo [msi] step: %%P.wixproj
  "%MSBUILD%" "%MSI%\%%P\%%P.wixproj" %COMMON% 2>&1
  if errorlevel 1 (echo [msi] %%P FAILED & exit /b 1)
)

echo [msi] step: rebuild PythonBA.dll bootstrap (v141_xp + SDK 7.1A + UCRT 10240)
if not defined VSROOT17 set "VSROOT17=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional"
set "MSBUILD17=%VSROOT17%\MSBuild\15.0\Bin\MSBuild.exe"
set "VCTOOLS17=%VSROOT17%\VC\Tools\MSVC\14.16.27023"
if not defined SDK71A set "SDK71A=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A"
if not defined SDK10 set "SDK10=C:\Program Files (x86)\Windows Kits\10"
set "UCRTVER=10.0.10240.0"
set "SDKVER=10.0.26100.0"
set "SDK71A_EXTRA=%~dp0Tools\xp\sdk71a-extra"
set "INCLUDE=%VCTOOLS17%\include;%SDK10%\Include\%UCRTVER%\ucrt;%SDK71A%\Include;%SDK71A_EXTRA%"
set "LIB=%VCTOOLS17%\lib\x86;%SDK10%\Lib\%UCRTVER%\ucrt\x86;%SDK71A%\Lib"
set "PATH=%VCTOOLS17%\bin\Hostx64\x86;%VCTOOLS17%\bin\Hostx64\x64;%VSROOT17%\Common7\IDE;%SDK71A%\Bin;%PATH%"
set "WindowsSDKDir_71A=%SDK71A%\"
"%MSBUILD17%" "%MSI%\bundle\bootstrap\pythonba.vcxproj" ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourcePath=%SRC%/ ^
  /p:PySourceDir=%SRC%/ ^
  /p:WixInstallPath=%WIX%\ ^
  2>&1
if errorlevel 1 (echo [msi] PythonBA v141_xp build FAILED & exit /b 1)
echo [msi] PythonBA v141_xp build OK

echo [msi] step: bundle snapshot.wixproj
"%MSBUILD%" "%MSI%\bundle\snapshot.wixproj" %COMMON% 2>&1
if errorlevel 1 (echo [msi] bundle FAILED & exit /b 1)

REM snapshot and releaselocal share the same msi_python wixobj directory;
REM snapshot compiles bundle.wxs with CompressMSI=no, so force releaselocal
REM to recompile (CompressMSI=yes) by removing the stale wixobj files.
del /q "%SRC%\PCbuild\obj\315win32_Release\msi_python\*.wixobj" 2>nul

echo [msi] step: bundle releaselocal.wixproj (self-contained installer)
"%MSBUILD%" "%MSI%\bundle\releaselocal.wixproj" %COMMON% /p:BuildForRelease=true 2>&1
if errorlevel 1 (echo [msi] releaselocal FAILED & exit /b 1)

echo [msi] step: collect artifacts into build\
REM The WiX projects emit into $(Py_OutDir)en-us; copy the bundle and the
REM sub-MSIs to build\ (the documented artifact location) before patching.
if not exist "%OUT%\en-us\python-3.15.0.exe" (echo [msi] bundle output missing & exit /b 1)
copy /y "%OUT%\en-us\python-3.15.0.exe" "%BUILD%\python-3.15.0.exe" >nul
if errorlevel 1 (echo [msi] copy bundle FAILED & exit /b 1)
copy /y "%OUT%\en-us\*.msi" "%BUILD%\" >nul

echo [msi] step: patch bundle PE subsystem to 5.1 for NT 5.1 loader
"%PYEXE%" "%~dp0Tools\xp\patch_pe_subsys.py" "%BUILD%\python-3.15.0.exe"
if errorlevel 1 (echo [msi] patch_pe_subsys FAILED & exit /b 1)

echo [msi] sub-MSI packages done
endlocal