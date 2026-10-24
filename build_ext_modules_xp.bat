@echo off
REM Build the XP-clean Windows extension modules for the layout tree.
REM _ssl/_hashlib link against the llvm-mingw-built OpenSSL (run
REM build_openssl_xp.bat first); the remaining modules in the loop need
REM only the externals headers/libs already vendored in externals\
REM (_ctypes/_bz2/_lzma/_sqlite3/_zstd included - IncludeExternals=false
REM only keeps their ProjectReferences inert, headers/libs resolve from
REM the env INCLUDE/LIB + python.props externals paths).
setlocal

if not defined VSROOT17 set "VSROOT17=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional"
set "MSBUILD17=%VSROOT17%\MSBuild\15.0\Bin\MSBuild.exe"
set "VCTOOLS17=%VSROOT17%\VC\Tools\MSVC\14.16.27023"
if not defined SDK71A set "SDK71A=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A"
set "SDK71A_EXTRA=%~dp0Tools\xp\sdk71a-extra"
if not defined SDK10 set "SDK10=C:\Program Files (x86)\Windows Kits\10"
set "UCRTVER=10.0.10240.0"
set "SDKVER=10.0.26100.0"
set "SRC=%~dp0."
set "OBJDIR=%~dp0build\out\obj"
set "PCBUILD=%~dp0build\out\pcbuild"
set "EXT=%~dp0externals"
set "OUT=%~dp0build\out\pcbuild\win32"
REM Build-assist interpreter; PYEXE overrides it (see README.md).
if not defined PYEXE (set "PYTHON=py") else set "PYTHON=%PYEXE%"
set "PythonForBuild=%PYTHON%"

set "INCLUDE=%VCTOOLS17%\include;%SDK10%\Include\%UCRTVER%\ucrt;%SDK71A%\Include;%SDK71A_EXTRA%"
set "LIB=%VCTOOLS17%\lib\x86;%SDK10%\Lib\%UCRTVER%\ucrt\x86;%SDK71A%\Lib"
set "LIB32=%LIB%"
set "PATH=%VCTOOLS17%\bin\Hostx64\x86;%VCTOOLS17%\bin\Hostx64\x64;%VSROOT17%\Common7\IDE;%SDK71A%\Bin;%PATH%"
set "WindowsSDKDir_71A=%SDK71A%\"

REM ===== _ssl + _hashlib: link against the llvm-mingw-built OpenSSL =====
REM (build_openssl_xp.bat must run first). openssl.props expects
REM libcrypto.lib / libssl.lib in opensslOutDir; the runtime DLLs are
REM already in the tree, so skip the props-side copy step.
REM UseEnv=true: the authoritative include/lib paths are the env, so
REM append OpenSSL there instead of relying on openssl.props metadata.
REM These two come AFTER the loop below: _ssl links _socket.lib, so
REM _socket.pyd must exist first (fresh builds have no stale copy).
set "INCLUDE=%INCLUDE%;%EXT%\openssl-3.5.9-src\include"
set "LIB=%LIB%;%EXT%\openssl-3.5.9-src"

REM ===== Runtime DLLs for the DLLs\ directory =====
REM The official build copies these via the IncludeExternals=true DLL-copy
REM steps, which we skip - so gather them explicitly (a fresh checkout has
REM none of them in the OutDir):
REM  - sqlite3.dll/.lib: built here (sqlite3.vcxproj); _sqlite3.pyd links
REM    sqlite3.lib and lib.msi ships the DLL into DLLs\.
REM  - libffi-8.dll: the XP-built runtime lib vendored in externals.
REM  - libcrypto-3/libssl-3.dll: from the llvm-mingw OpenSSL build.
REM  - zlib1/libtommath/tcl9dde14/tcl9registry13.dll: from the tcl nmake
REM    output (build_tcltk_xp.bat runs first).
echo [xp-ext] sqlite3.dll (v141_xp + Py_XP)...
"%MSBUILD17%" "%SRC%\PCbuild\sqlite3.vcxproj" /t:Build /nologo /v:q /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v141_xp /p:WindowsTargetPlatformVersion=%SDKVER% /p:UCRTVersion=%UCRTVER% /p:UseEnv=true /p:XPBuild=true /p:IncludeExternals=true /p:BuildProjectReferences=false /p:ExternalsDir=%EXT%\ /p:PySourceDir="%SRC%\\" /p:PySourcePath="%SRC%\\" /p:GIT="" /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%"
if errorlevel 1 (echo [xp-ext] sqlite3.dll FAILED & exit /b 1)
copy /y "%EXT%\libffi-3.4.4\win32\libffi-8.dll" "%OUT%\" >nul
copy /y "%EXT%\openssl-3.5.9-src\libcrypto-3.dll" "%OUT%\" >nul
copy /y "%EXT%\openssl-3.5.9-src\libssl-3.dll" "%OUT%\" >nul
copy /y "%EXT%\tcl-9.0.4.0\win\Release_VC1916\zlib1.dll" "%OUT%\" >nul
copy /y "%EXT%\tcl-9.0.4.0\win\Release_VC1916\libtommath.dll" "%OUT%\" >nul
copy /y "%EXT%\tcl-9.0.4.0\win\Release_VC1916\tcl9dde14.dll" "%OUT%\" >nul
copy /y "%EXT%\tcl-9.0.4.0\win\Release_VC1916\tcl9registry13.dll" "%OUT%\" >nul

REM modules that build with zero external source deps
REM (single-line MSBuild invocation: ^ continuations inside for-blocks are
REM not reliable under cmd.exe)
REM IncludeExternals=false is mandatory: pythoncore's zlib-ng ProjectReference
REM stays inert, so the v141_xp toolchain never rebuilds zlib-ng (its AVX512
REM settings are invalid for v141). The v143-built zlib-ng.lib is already in
REM the OutDir from build_python_xp.bat step 1.5.
REM ExternalsDir MUST end with a trailing backslash: python.props joins
REM $(ExternalsDir)libffi-3.4.4\ etc. without its own separator, so a
REM missing trailing slash silently produces "externalslibffi-..." and the
REM compiler cannot find ffi.h.
REM _remote_debugging is intentionally excluded: it reads
REM SYSTEM_PROCESS_INFORMATION from NtQuerySystemInformation and relies on
REM the 26100-SDK field layout (NumberOfThreads etc.); 7.1A headers (and
REM the NT 5.1 kernel's actual layout) use the legacy layout without that
REM field, so a faithful XP port needs layout-aware parsing, not a header
REM shim. It is a 3.15 profiling tool that does not exist in 3.14, so the
REM 3.14 XP branch simply has no such module in the build list.
for %%M in (_asyncio _decimal _elementtree _multiprocessing _overlapped pyexpat _queue select unicodedata winsound _uuid _zoneinfo _socket _ctypes _bz2 _lzma _sqlite3 _zstd _wmi) do (
  echo [xp-ext] %%M ...
  "%MSBUILD17%" "%SRC%\PCbuild\%%M.vcxproj" /t:Build /nologo /v:q /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v141_xp /p:WindowsTargetPlatformVersion=%SDKVER% /p:UCRTVersion=%UCRTVER% /p:UseEnv=true /p:XPBuild=true /p:IncludeExternals=false /p:ExternalsDir=%EXT%\ /p:PySourceDir="%SRC%\\" /p:PySourcePath="%SRC%\\" /p:GIT="" /p:PYTHON="%PYTHON%" /p:PythonForBuild="%PYTHON%" /p:Py_IntDir="%OBJDIR%" /p:Py_OutDir="%PCBUILD%"
  if errorlevel 1 echo [xp-ext] %%M FAILED
)

echo [xp-ext] _ssl (IncludeSSL=true)...
"%MSBUILD17%" "%SRC%\PCbuild\_ssl.vcxproj" /t:Build /nologo /v:m /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v141_xp /p:WindowsTargetPlatformVersion=%SDKVER% /p:UCRTVersion=%UCRTVER% /p:UseEnv=true /p:XPBuild=true /p:IncludeSSL=true /p:IncludeExternals=true /p:BuildProjectReferences=false /p:ExternalsDir=%EXT%\ /p:opensslOutDir=%EXT%\openssl-3.5.9-src\ /p:SkipCopySSLDLL=true /p:PySourceDir="%SRC%\\" /p:PySourcePath="%SRC%\\" /p:GIT="" /p:PYTHON=%PYTHON% /p:PythonForBuild=%PYTHON% /p:Py_IntDir=%OBJDIR% /p:Py_OutDir=%PCBUILD% 2>&1
if errorlevel 1 (echo [xp-ext] _ssl FAILED & exit /b 1)
echo [xp-ext] _hashlib (IncludeSSL=true)...
"%MSBUILD17%" "%SRC%\PCbuild\_hashlib.vcxproj" /t:Build /nologo /v:m /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v141_xp /p:WindowsTargetPlatformVersion=%SDKVER% /p:UCRTVersion=%UCRTVER% /p:UseEnv=true /p:XPBuild=true /p:IncludeSSL=true /p:IncludeExternals=true /p:BuildProjectReferences=false /p:ExternalsDir=%EXT%\ /p:opensslOutDir=%EXT%\openssl-3.5.9-src\ /p:SkipCopySSLDLL=true /p:PySourceDir="%SRC%\\" /p:PySourcePath="%SRC%\\" /p:GIT="" /p:PYTHON=%PYTHON% /p:PythonForBuild=%PYTHON% /p:Py_IntDir=%OBJDIR% /p:Py_OutDir=%PCBUILD% 2>&1
if errorlevel 1 (echo [xp-ext] _hashlib FAILED & exit /b 1)
echo [xp-ext] done
endlocal