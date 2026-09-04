@echo off
rem Fragmentation Governor CUDA proof build helper (portable).
rem Requires: CUDA_PATH set to a CUDA toolkit root, and a Visual Studio with the
rem C++ workload (resolved via vswhere).
setlocal
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" ( echo VSWHERE_NOT_FOUND & exit /b 1 )
for /f "tokens=*" %%i in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set VSINSTALL=%%i
if not defined VSINSTALL ( echo VS_NOT_FOUND & exit /b 1 )
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not defined CUDA_PATH ( echo CUDA_PATH_NOT_SET & exit /b 1 )
set NVCC=%CUDA_PATH%\bin\nvcc.exe
if not exist "%NVCC%" ( echo NVCC_NOT_FOUND & exit /b 1 )
set ROOT=%~dp0..
pushd "%ROOT%\cuda"
"%NVCC%" -arch=sm_120 -std=c++20 -Xcompiler /MD -I"%ROOT%\include" fg_cuda_proof.cu "%ROOT%\build\Release\FragmentationGovernor.lib" -o fg_cuda_proof.exe
if errorlevel 1 ( echo NVCC_BUILD_FAILED & popd & exit /b 1 )
echo NVCC_BUILD_OK
popd
