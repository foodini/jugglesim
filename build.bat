@echo off
rem build.bat - command-line build for JuggleSim.
rem
rem   build.bat [Debug|Release] [Build|Rebuild|Clean]      (defaults: Debug Build)
rem
rem Writes the full MSBuild output to build.log and just the errors and warnings to
rem build_issues.log, both next to this script. Exit code is MSBuild's (0 = success).

setlocal EnableExtensions
cd /d "%~dp0"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
set "TARGET=%~2"
if "%TARGET%"=="" set "TARGET=Build"

if /i not "%CONFIG%"=="Debug" if /i not "%CONFIG%"=="Release" (
    echo Unknown configuration "%CONFIG%". Use Debug or Release.
    exit /b 2
)
if /i not "%TARGET%"=="Build" if /i not "%TARGET%"=="Rebuild" if /i not "%TARGET%"=="Clean" (
    echo Unknown target "%TARGET%". Use Build, Rebuild or Clean.
    exit /b 2
)

if not exist "third_party\imgui\imgui.cpp" (
    echo Dear ImGui is missing. Run:  git submodule update --init
    exit /b 2
)

rem Locate MSBuild through vswhere (ships with every Visual Studio 2017+ install).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found at "%VSWHERE%". Is Visual Studio installed?
    exit /b 2
)
set "MSBUILD="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    if not defined MSBUILD set "MSBUILD=%%i"
)
if not defined MSBUILD (
    echo Could not find MSBuild.exe via vswhere.
    exit /b 2
)

echo Building jugglesim %CONFIG%^|x64 [%TARGET%]
echo Using "%MSBUILD%"

"%MSBUILD%" jugglesim.sln -t:%TARGET% -p:Configuration=%CONFIG% -p:Platform=x64 -m -nologo ^
    -verbosity:minimal -fileLogger -fileLoggerParameters:LogFile=build.log;Verbosity=normal;NoSummary;Encoding=UTF-8
set "RESULT=%ERRORLEVEL%"

rem Pull errors and warnings out of the full log. (NoSummary above keeps them from being
rem repeated at the end of build.log, so each one appears here once.)
findstr /r /c:": error " /c:": fatal error " /c:": warning " build.log > build_issues.log

echo.
for %%A in (build_issues.log) do if %%~zA==0 (
    echo No errors or warnings.
) else (
    type build_issues.log
)
echo.
if "%RESULT%"=="0" (echo BUILD SUCCEEDED) else (echo BUILD FAILED ^(exit code %RESULT%^))
exit /b %RESULT%
