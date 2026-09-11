@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL exit /b 1
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
if not exist native\build mkdir native\build
if not exist native\dist mkdir native\dist
cl /nologo /MT /EHsc /std:c++17 /W4 /WX /O2 native\pc\SharedTextureTest.cpp /Fo:native\build\SharedTextureTest.obj /link d3d11.lib dxgi.lib /OUT:native\dist\SharedTextureTest.exe
exit /b %errorlevel%
