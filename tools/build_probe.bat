@echo off
rem build_probe.bat - builds tools\spacemouse_probe.exe (see spacemouse_probe.cpp).
setlocal EnableExtensions
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found at "%VSWHERE%". Is Visual Studio installed?
    exit /b 2
)
set "VCVARS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -find VC\Auxiliary\Build\vcvars64.bat`) do (
    if not defined VCVARS set "VCVARS=%%i"
)
if not defined VCVARS (
    echo Could not find vcvars64.bat via vswhere.
    exit /b 2
)
call "%VCVARS%" >nul
cl /nologo /EHsc /W4 /O2 spacemouse_probe.cpp /Fe:spacemouse_probe.exe /Fo:spacemouse_probe.obj user32.lib
exit /b %ERRORLEVEL%
