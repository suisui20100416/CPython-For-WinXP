@echo off
REM Goal: one-command XP build of CPython core on the xp branch.
REM
REM Toolchain split (measured, see Python/STAGE-C-结论.md section 8):
REM  - The freeze tool (_freeze_module.exe) and zlib-ng.lib are *build-time*
REM    artifacts; they do not have to run on XP. Build them with the official
REM    v143 toolchain (VS2022 14.44.35207 + SDK 10.0.26100). v141_xp shows a
REM    compiler-codegen regression with 3.15's static-type machinery
REM    (RecursionError inside PyErr_NewException -> type_ready) -- every v141
REM    freeze build recurses, every v143 build of identical source succeeds.
REM  - The XP binaries (python315.dll / python.exe / pythonw.exe) are built
REM    with v141_xp (VS2017 14.16.27023) + SDK 7.1A headers + UCRT 10.0.10240
REM    (the XP-clean UCRT generation; measured in xp-build/tests/crt_matrix/)
REM    + /MT static CRT + UseEnv=true so the linker takes the env LIB (7.1A +
REM    10240), never the registered 26100 UCRT (which pulls in FlsAlloc/
REM    InitializeCriticalSectionEx -- Vista+).
REM
REM  - /DPy_XP=1 (via /p:XPBuild=true -> pyproject.props) switches the source
REM    to the XP compatibility vocabulary (PC/xp_compat.h).
REM  - Win32 platform only in this pass.
REM
REM All artifacts go to build\ : PC build output in build\out\pcbuild,
REM intermediates in build\out\obj; nothing in the source tree beyond the
REM xp-branch source patches already committed.
setlocal EnableDelayedExpansion

REM Toolchain roots can be overridden via the environment before invoking;
REM the defaults below match a standard VS2017 Professional + VS2022
REM BuildTools + Windows Kits 10 install on x64 Windows.
if not defined VSROOT17 set "VSROOT17=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional"
set "MSBUILD17=%VSROOT17%\MSBuild\15.0\Bin\MSBuild.exe"
set "VCTOOLS17=%VSROOT17%\VC\Tools\MSVC\14.16.27023"
if not defined SDK71A set "SDK71A=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A"
set "SDK71A_EXTRA=%~dp0Tools\xp\sdk71a-extra"

if not defined VSROOT22 set "VSROOT22=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "MSBUILD22=%VSROOT22%\MSBuild\Current\Bin\MSBuild.exe"
set "VCTOOLS22=%VSROOT22%\VC\Tools\MSVC\14.44.35207"

if not defined SDK10 set "SDK10=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"
set "UCRTVER=10.0.10240.0"
set "SRC=%~dp0."
set "EXT=%~dp0externals"
set "OBJDIR=%~dp0build\out\obj"
set "PCBUILD=%~dp0build\out\pcbuild"
set "OUT=%~dp0build\out\pcbuild\win32"

REM Python host for build scripts ("py" resolves to any installed 3.15
REM runtime - only drives regen and auxiliary scripts; a space-free name
REM avoids MSBuild argument splitting). PYEXE overrides it (see README.md).
if not defined PYEXE (set "PYTHON=py") else set "PYTHON=%PYEXE%"
set "PythonForBuild=%PYTHON%"

if not exist "%OUT%" mkdir "%OUT%"

REM ===== Step 1: freeze tool with the OFFICIAL toolchain (v143) ==============
REM Official SDK um/shared headers, default UCRT, no XP vocabulary. It uses
REM the xp branch's conditional includes (#ifndef Py_XP -> the real headers),
REM so both worlds compile from one source tree.
set "INCLUDE=%VCTOOLS22%\include;%SDK10%\Include\%SDKVER%\ucrt;%SDK10%\Include\%SDKVER%\shared;%SDK10%\Include\%SDKVER%\um;%SDK10%\Include\%SDKVER%\winrt"
set "LIB=%VCTOOLS22%\lib\x86;%SDK10%\Lib\%SDKVER%\ucrt\x86;%SDK10%\Lib\%SDKVER%\um\x86"
set "PATH=%VCTOOLS22%\bin\Hostx64\x86;%VCTOOLS22%\bin\Hostx64\x64;%PATH%"

echo [xp-build] step 1/2: _freeze_module (v143, official headers)...
"%MSBUILD22%" "%SRC%\PCbuild\_freeze_module.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v143 ^
  /p:RuntimeLibrary=MultiThreaded ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:IncludeExternals=false ^
  /p:IncludeCTypes=false /p:IncludeSSL=false /p:IncludeTkinter=false ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] freeze FAILED & exit /b 1)

REM ===== Step 1.5: zlib-ng static lib with the OFFICIAL toolchain (v143) ====
REM Static .lib links fine across toolsets (same machine ABI), so the
REM XP-built pythoncore can consume a v143-built zlib-ng.lib. Building it
REM here keeps IncludeExternals=false for pythoncore (no sqlite/ssl/etc).
echo [xp-build] step 1.5: zlib-ng (v143, /MT, static lib)...
"%MSBUILD22%" "%SRC%\PCbuild\zlib-ng.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v143 ^
  /p:RuntimeLibrary=MultiThreaded ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:IntDir=%OBJDIR%\315win32_Release\zlib-ng\ ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] zlib-ng FAILED & exit /b 1)

REM zlib-ng.vcxproj writes to PCbuild\win32 regardless of Py_OutDir;
REM mirror the lib into our OutDir where pythoncore links from it.
echo f | xcopy /Y "%SRC%\PCbuild\win32\zlib-ng.lib" "%OUT%\" >nul

REM Generate the zlib.h / zlib-ng.h / zconf.h / zlib_name_mangling.h
REM binary headers exactly like PCbuild\zlib-ng.vcxproj's
REM _EnsureZlibH/_EnsureZlibNgH do (replace the symbol-prefix placeholder);
REM the project's own target does not fire reliably from this script's
REM MSBuild invocation, so do it here.
set "ZNGBIN=%OBJDIR%\315win32_Release\zlib-ng"
if not exist "%ZNGBIN%" mkdir "%ZNGBIN%"
powershell -NoProfile -Command ^
  "$z=Get-Content -Raw '%EXT%\zlib-ng-2.2.4\zlib.h.in'; $z=$z.Replace('@ZLIB_SYMBOL_PREFIX@',''); [IO.File]::WriteAllText('%ZNGBIN%\zlib.h',$z,[Text.Encoding]::UTF8); " ^
  "$z=Get-Content -Raw '%EXT%\zlib-ng-2.2.4\zlib-ng.h.in'; $z=$z.Replace('@ZLIB_SYMBOL_PREFIX@',''); [IO.File]::WriteAllText('%ZNGBIN%\zlib-ng.h',$z,[Text.Encoding]::UTF8); " ^
  "$z=Get-Content -Raw '%EXT%\zlib-ng-2.2.4\zconf.h.in'; [IO.File]::WriteAllText('%ZNGBIN%\zconf.h',$z,[Text.Encoding]::UTF8); " ^
  "$z=Get-Content -Raw '%EXT%\zlib-ng-2.2.4\zlib_name_mangling.h.in'; $z=$z.Replace('@ZLIB_SYMBOL_PREFIX@',''); [IO.File]::WriteAllText('%ZNGBIN%\zlib_name_mangling.h',$z,[Text.Encoding]::UTF8)"

REM ===== Steps 2-3: XP runtime (python315.dll + python.exe + pythonw.exe) ===
REM v141_xp + SDK 7.1A headers + UCRT 10240 + /MT + XPBuild=true.
REM UseEnv=true: the linker takes the env LIB (7.1A + 10240 ucrt) instead of
REM the registered 26100 SDK -- this is what keeps FlsAlloc & friends out of
REM the import table (26100 libucrt pulls them in, see crt_matrix).
set "INCLUDE=%VCTOOLS17%\include;%SDK10%\Include\%UCRTVER%\ucrt;%SDK71A%\Include;%SDK71A_EXTRA%"
set "LIB=%VCTOOLS17%\lib\x86;%SDK10%\Lib\%UCRTVER%\ucrt\x86;%SDK71A%\Lib"
set "LIB32=%LIB%"
set "PATH=%VCTOOLS17%\bin\Hostx64\x86;%VCTOOLS17%\bin\Hostx64\x64;%VSROOT17%\Common7\IDE;%SDK71A%\Bin;%PATH%"
set "WindowsSDKDir_71A=%SDK71A%\"

echo [xp-build] step 2/2: pythoncore (v141_xp + 7.1A + UCRT %UCRTVER% + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\pythoncore.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:IncludeExternals=false ^
  /p:IncludeCTypes=false /p:IncludeSSL=false /p:IncludeTkinter=false ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] pythoncore FAILED & exit /b 1)

REM ===== Step 3.5: python3.dll / python3t.dll stable ABI forwarders =========
REM 3.15 layout requires both (python3t.dll is the freethreaded stable
REM forwarder; it is a pure NoEntryPoint forwarder from PC/python3dll.c, no
REM freethreaded runtime needed for a GIL-only distribution).
echo [xp-build] step 3.5: python3.dll (v141_xp + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\python3dll.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] python3.dll FAILED & exit /b 1)

echo [xp-build] step 3.5: python3t.dll (v141_xp + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\python3tdll.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] python3t.dll FAILED & exit /b 1)

REM ===== Step 3.7: venv launchers + py launchers (v141_xp + Py_XP) =========
REM Layout needs venvlauncher/venvwlauncher (Lib/venv/scripts/nt) and
REM pylauncher/pywlauncher (py.exe/pyw.exe alias entry points). Each is an
REM explicit block: a for-loop over the four projects trips over the ^
REM continuations inside the parenthesis block under cmd.exe.

echo [xp-build] step 3.7: venvlauncher (v141_xp + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\venvlauncher.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] venvlauncher FAILED & exit /b 1)

echo [xp-build] step 3.7: venvwlauncher (v141_xp + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\venvwlauncher.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] venvwlauncher FAILED & exit /b 1)

echo [xp-build] step 3.7: pylauncher (v141_xp + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\pylauncher.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] pylauncher FAILED & exit /b 1)

echo [xp-build] step 3.7: pywlauncher (v141_xp + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\pywlauncher.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] pywlauncher FAILED & exit /b 1)

echo [xp-build] step 3/3: python.exe + pythonw.exe (v141_xp + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\python.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:IncludeExternals=false ^
  /p:IncludeCTypes=false /p:IncludeSSL=false /p:IncludeTkinter=false ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] python.exe FAILED & exit /b 1)

"%MSBUILD17%" "%SRC%\PCbuild\pythonw.vcxproj" ^
  /t:Build ^
  /nologo /v:m /p:Configuration=Release /p:Platform=Win32 ^
  /p:PlatformToolset=v141_xp ^
  /p:WindowsTargetPlatformVersion=%SDKVER% ^
  /p:UCRTVersion=%UCRTVER% ^
  /p:IncludeExternals=false ^
  /p:IncludeCTypes=false /p:IncludeSSL=false /p:IncludeTkinter=false ^
  /p:UseEnv=true ^
  /p:XPBuild=true ^
  /p:ExternalsDir=%EXT%\ ^
  /p:PySourceDir="%SRC%\" ^
  /p:PySourcePath="%SRC%\" ^
  /p:GIT="" ^
  /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" ^
  /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%" ^
  2>&1
if errorlevel 1 (echo [xp-build] pythonw.exe FAILED & exit /b 1)

echo [xp-build] OK
endlocal