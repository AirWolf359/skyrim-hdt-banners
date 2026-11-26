@echo off
rem Builds build\release\BannerPhysics.dll and .pdb.
rem Needs Visual Studio 2022+ or Build Tools with the C++ workload (MSVC, CMake, Ninja, vcpkg),
rem and the submodules: git submodule update --init --recursive
rem
rem The build runs from a substituted drive (B: by default, override with BP_DRIVE) so that every path
rem recorded in the DLL and PDB is B:\... and not the local checkout path, which usually contains the
rem Windows user name. vcpkg libraries are built in the same tree, never restored from the binary cache,
rem so their debug info carries the same neutral paths.
setlocal
cd /d "%~dp0"

if not exist "extern\CommonLibVR\CMakeLists.txt" (
  echo Submodules are missing. Run: git submodule update --init --recursive
  exit /b 1
)

if not defined BP_DRIVE set "BP_DRIVE=B"
set "BP_ROOT=%BP_DRIVE%:"
if exist "%BP_ROOT%\" (
  echo Drive %BP_ROOT% is already in use. Set BP_DRIVE to a free drive letter, for example: set BP_DRIVE=W
  exit /b 1
)
subst %BP_ROOT% "%CD%" || exit /b 1
call :build
set "RESULT=%ERRORLEVEL%"
cd /d "%~dp0"
subst %BP_ROOT% /d
exit /b %RESULT%

:build
cd /d "%BP_ROOT%\"

rem Kept outside any ( ) block: %ProgramFiles(x86)% contains a ")" that would end the block.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if defined VSCMD_VER goto :vcvars_ready
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo Visual Studio with the C++ workload was not found.
  exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
:vcvars_ready

if not defined VCPKG_ROOT set "VCPKG_ROOT=%VCINSTALLDIR%vcpkg"
set "VCPKG_BINARY_SOURCES=clear"
if not exist "%BP_ROOT%\build\tmp" mkdir "%BP_ROOT%\build\tmp"
set "TEMP=%BP_ROOT%\build\tmp"
set "TMP=%BP_ROOT%\build\tmp"

cmake --preset release || exit /b 1
cmake --build --preset release || exit /b 1
exit /b 0
