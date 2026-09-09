@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL exit /b 1
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
cl /nologo /LD /MT /EHsc /O2 native\pc\SwDeviceBridge.cpp /Fo:native\build\SwDeviceBridge.obj /link Swdevice.lib /OUT:native\dist\WiredScreen.Native.dll /IMPLIB:native\build\WiredScreen.Native.lib
exit /b %errorlevel%
