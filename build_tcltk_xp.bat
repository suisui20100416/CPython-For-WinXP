@echo off
REM Build the whole tcl/tk family for XP in one shot:
REM   step 1  tcl 9.0.4   - nmake OPTS=msvcrt,nomsvcrt (/MT static UCRT)
REM   step 2  tk  9.0.4.1 - nmake, mirror the DLL into every consumer tree
REM   step 3  _tkinter.pyd - cl + link against the /MT tcl/tk libs
REM   step 4  tcl/tk script libraries staged from the zip archives
REM Toolchain: v141_xp env (vcvars32) + SDK 7.1A headers + UCRT 10240.
REM XP specifics live in the vendored sources directly: the ITaskbarList3
REM null checks in tkWinWm.c and the -subsystem/-osversion 5.01 pins in
REM win/rules.vc (18-test: the build-time patch scripts were retired).
REM Rebuild reproducibility notes live in Python/STAGE-C-结论.md 8.18/8.29.
setlocal

if not defined VSROOT17 set "VSROOT17=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional"
call "%VSROOT17%\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
if not defined SDK71A set "SDK71A=C:\Program Files (x86)\Microsoft SDKs\Windows\v7.1A"
if not defined SDK10 set "SDK10=C:\Program Files (x86)\Windows Kits\10"
set "UCRTVER=10.0.10240.0"
set "SDK71A_EXTRA=%~dp0Tools\xp\sdk71a-extra"
set "INCLUDE=%INCLUDE%;%SDK10%\Include\%UCRTVER%\ucrt;%SDK71A%\Include;%SDK71A_EXTRA%"
set "LIB=%SDK10%\Lib\%UCRTVER%\ucrt\x86;%SDK71A%\Lib;%LIB%"
set "WindowsSDKDir_71A=%SDK71A%\"
set "VCINSTALLDIR=%VSROOT17%\VC"

set "SRC=%~dp0."
set "TCLSRC=%~dp0externals\tcl-9.0.4.0"
set "TKSRC=%~dp0externals\tk-9.0.4.1"
set "TCLTK=%~dp0externals\tcltk-9.0.4.0\win32"
set "PCBUILD=%~dp0build\out\pcbuild"
set "OUT=%~dp0build\out\pcbuild\win32"
set "LAYOUT=%~dp0build\out"
set "OBJDIR=%~dp0build\out\obj"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
REM Fresh-checkout builds have no layout tree yet; create the mirror
REM targets so the copies below cannot silently fail.
if not exist "%LAYOUT%\DLLs" mkdir "%LAYOUT%\DLLs"
if not exist "%OUT%\DLLs" mkdir "%OUT%\DLLs"

REM ===== Step 1: tcl =====
REM win/rules.vc pins -subsystem/-osversion 5.01 on every image, so the
REM DLLs come out of nmake XP-loadable directly (no post-build PE patch).
cd /d "%TCLSRC%\win"
nmake -nologo -f makefile.vc release OPTS=msvcrt,nomsvcrt 1>"%OBJDIR%\tcl_build.log" 2>&1
if errorlevel 1 (echo [tcltk] tcl nmake FAILED & type "%OBJDIR%\tcl_build.log" & exit /b 1)

REM Mirror tcl90.dll into every tree the installer/layout pulls from and
REM refresh the import library _tkinter links against - without this a
REM fresh checkout has no tcl90.dll anywhere in the build tree.
copy /y "%TCLSRC%\win\Release_VC1916\tcl90.dll" "%OUT%\tcl90.dll" >nul
copy /y "%TCLSRC%\win\Release_VC1916\tcl90.dll" "%TCLTK%\bin\tcl90.dll" >nul
copy /y "%TCLSRC%\win\Release_VC1916\tcl90.dll" "%LAYOUT%\DLLs\tcl90.dll" >nul
copy /y "%TCLSRC%\win\Release_VC1916\tcl90.lib" "%TCLTK%\lib\tcl90.lib" >nul
copy /y "%TCLSRC%\win\Release_VC1916\tcl9dde14.dll" "%OUT%\tcl9dde14.dll" >nul
copy /y "%TCLSRC%\win\Release_VC1916\tcl9registry13.dll" "%OUT%\tcl9registry13.dll" >nul
echo [tcltk] step 1/4: tcl 9.0.4 OK (tcl90.dll 5.01 and mirrored)

REM ===== Step 2: tk =====
REM The ITaskbarList3 null checks live in the vendored tkWinWm.c source
REM directly (see Python/STAGE-C 8.29 for the crash analysis).
cd /d "%TKSRC%\win"
nmake -nologo -f makefile.vc release TCLDIR=%TCLSRC% OPTS=msvcrt,nomsvcrt 1>"%OBJDIR%\tk_build.log" 2>&1
if errorlevel 1 (echo [tcltk] tk nmake FAILED & type "%OBJDIR%\tk_build.log" & exit /b 1)

REM Mirror the DLL into every tree the installer may pull from.
copy /y "%TKSRC%\win\Release_VC1916\tcl9tk90.dll" "%OUT%\tcl9tk90.dll" >nul
copy /y "%TKSRC%\win\Release_VC1916\tcl9tk90.dll" "%TCLTK%\bin\tcl9tk90.dll" >nul
copy /y "%TKSRC%\win\Release_VC1916\tcl9tk90.dll" "%LAYOUT%\DLLs\tcl9tk90.dll" >nul
echo [tcltk] step 2/4: tk 9.0.4.1 OK (tcl9tk90.dll 5.01 and mirrored)

REM ===== Step 3: _tkinter.pyd =====
REM The /DPy_TCLTK_DIR value must be a quoted string literal for C,
REM but cmd corrupts backslash-quote sequences on the cl command line
REM (D8003 "missing source file"). A response file is parsed by cl
REM itself, so the quoting survives.
cd /d "%~dp0PCbuild"
set "RSP1=%OBJDIR%\_tkinter_cl.rsp"
> "%RSP1%" echo /nologo /c /I"%TCLTK%\include" /I"%~dp0Include" /I"%~dp0Include\internal" /I"%~dp0PC"
>>"%RSP1%" echo /DWIN32 /D_WIN32 /DNDEBUG /D_WINDLL /D_USING_V110_SDK71_ /DWITH_APPINIT /DTCL_WITH_EXTERNAL_TOMMATH
>>"%RSP1%" echo /DPy_TCLTK_DIR="\"%~dp0externals/tcltk-9.0.4.0/win32""
>>"%RSP1%" echo /Fo"%OUT%\_tkinter.obj"
>>"%RSP1%" echo "%~dp0Modules\_tkinter.c"
cl @"%RSP1%" 1>"%OBJDIR%\tkinter_build.log" 2>&1
if errorlevel 1 (echo [tcltk] compile _tkinter FAILED & type "%OBJDIR%\tkinter_build.log" & exit /b 1)
set "RSP2=%OBJDIR%\tkappinit_cl.rsp"
> "%RSP2%" echo /nologo /c /I"%TCLTK%\include" /I"%~dp0Include" /I"%~dp0PC"
>>"%RSP2%" echo /DWIN32 /D_WIN32 /DNDEBUG /D_WINDLL /D_USING_V110_SDK71_ /DWITH_APPINIT
>>"%RSP2%" echo /Fo"%OUT%\tkappinit.obj"
>>"%RSP2%" echo "%~dp0Modules\tkappinit.c"
cl @"%RSP2%" 1>>"%OBJDIR%\tkinter_build.log" 2>&1
if errorlevel 1 (echo [tcltk] compile tkappinit FAILED & type "%OBJDIR%\tkinter_build.log" & exit /b 1)

cd /d "%OUT%"
link /nologo /dll /machine:x86 /subsystem:windows,5.1 /out:%OUT%\_tkinter.pyd ^
  %OUT%\_tkinter.obj %OUT%\tkappinit.obj ^
  %OUT%\python315.lib ^
  "%TCLTK%\lib\tcl90.lib" "%TCLTK%\lib\tcl9tk90.lib" "%TCLTK%\lib\tommath.lib" ^
  kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib ws2_32.lib comdlg32.lib comctl32.lib uuid.lib imm32.lib winspool.lib oleaut32.lib netapi32.lib userenv.lib 1>>"%OBJDIR%\tkinter_build.log" 2>&1
if errorlevel 1 (echo [tcltk] link _tkinter FAILED & type "%OBJDIR%\tkinter_build.log" & exit /b 1)
copy /y "%OUT%\_tkinter.pyd" "%OUT%\DLLs\_tkinter.pyd" >nul
copy /y "%OUT%\_tkinter.pyd" "%LAYOUT%\DLLs\_tkinter.pyd" >nul
echo [tcltk] step 3/4: _tkinter.pyd OK (mirrored to DLLs trees)

REM ===== Step 4: tcl/tk script libraries =====
REM lib.msi collects $(OutDir)DLLs\tcl\** and DLLs\Tk\** and installs them
REM as the root tcl\tcl9.0 / tcl\tk9.0 directories (Tcl_FindExecutable and
REM Lib/tkinter/__init__.py look there). The libraries ship as zip
REM archives in the tcltk externals; extract them plus the dde1.4 /
REM registry1.3 package directories. Without this step the MSI wildcard
REM matches nothing and the installer silently omits the script libraries
REM - import tkinter then fails on a clean machine (2026-10-24 finding:
REM the 11-test build was 16827306 bytes instead of 18163319 and still
REM passed on the test machine via leftover tcl\tcl9.0 from an install).
rd /s /q "%OBJDIR%\tcllib" 2>nul
rd /s /q "%OBJDIR%\tklib" 2>nul
py -m zipfile -e "%TCLTK%\lib\libtcl9.0.4.zip" "%OBJDIR%\tcllib"
if errorlevel 1 (echo [tcltk] extract libtcl zip FAILED & exit /b 1)
py -m zipfile -e "%TCLTK%\lib\libtk9.0.4.zip" "%OBJDIR%\tklib"
if errorlevel 1 (echo [tcltk] extract libtk zip FAILED & exit /b 1)
rd /s /q "%OUT%\DLLs\tcl" 2>nul
rd /s /q "%OUT%\DLLs\Tk" 2>nul
robocopy "%OBJDIR%\tcllib\tcl_library" "%OUT%\DLLs\tcl" /e /move /njh /njs /ndl /nc /ns /np >nul
if errorlevel 8 (echo [tcltk] stage tcl_library FAILED & exit /b 1)
robocopy "%OBJDIR%\tklib\tk_library" "%OUT%\DLLs\Tk" /e /move /njh /njs /ndl /nc /ns /np >nul
if errorlevel 8 (echo [tcltk] stage tk_library FAILED & exit /b 1)
rd /s /q "%OBJDIR%\tcllib" 2>nul
rd /s /q "%OBJDIR%\tklib" 2>nul
xcopy /e /i /y "%TCLTK%\lib\dde1.4" "%OUT%\DLLs\tcl\dde1.4" >nul
xcopy /e /i /y "%TCLTK%\lib\registry1.3" "%OUT%\DLLs\tcl\registry1.3" >nul
REM The win32\lib snapshot's dde/registry DLLs are the official v143
REM builds linked against the dynamic UCRT - api-ms-win-crt-* imports
REM that XP cannot load.  Keep only their pkgIndex.tcl and overwrite
REM the DLLs with our /MT builds from step 1 (16-test import-audit
REM finding: the dirty copies reached lib.msi via this xcopy).
copy /y "%OUT%\tcl9dde14.dll" "%OUT%\DLLs\tcl\dde1.4\tcl9dde14.dll" >nul
copy /y "%OUT%\tcl9registry13.dll" "%OUT%\DLLs\tcl\registry1.3\tcl9registry13.dll" >nul
REM The local smoke test runs %OUT%\python.exe; get_tcl_lib_path looks
REM up prefix\tcl\tcl9.0 / prefix\tcl\tk9.0, so mirror the same trees
REM under the build prefixes (pcbuild\win32 and the layout root).
for %%R in ("%OUT%" "%LAYOUT%") do (
  rd /s /q "%%~fR\tcl" 2>nul
  xcopy /e /i /y "%OUT%\DLLs\tcl" "%%~fR\tcl\tcl9.0" >nul
  xcopy /e /i /y "%OUT%\DLLs\Tk" "%%~fR\tcl\tk9.0" >nul
)
echo [tcltk] step 4/4: tcl/tk script libraries staged (DLLs\tcl, DLLs\Tk, tcl\tcl9.0, tcl\tk9.0)
echo [tcltk] all done
endlocal
