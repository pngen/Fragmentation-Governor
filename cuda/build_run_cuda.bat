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
set FG_LIB=%ROOT%\build\Release\FragmentationGovernor.lib
pushd "%ROOT%\cuda"
echo --- building fg_cuda_proof ---
"%NVCC%" -arch=sm_120 -std=c++20 -Xcompiler /MD -I"%ROOT%\include" fg_cuda_proof.cu "%FG_LIB%" -o fg_cuda_proof.exe
if errorlevel 1 ( echo NVCC_PROOF_FAILED & popd & exit /b 1 )
echo --- building fg_cuda_worker ---
"%NVCC%" -arch=sm_120 -std=c++20 -Xcompiler /MD -I"%ROOT%\include" fg_cuda_worker.cu "%FG_LIB%" ws2_32.lib -o fg_cuda_worker.exe
if errorlevel 1 ( echo NVCC_WORKER_FAILED & popd & exit /b 1 )
echo --- building fg_cuda_scenario_f ---
"%NVCC%" -arch=sm_120 -std=c++20 -Xcompiler /MD -I"%ROOT%\include" fg_cuda_scenario_f.cu "%FG_LIB%" ws2_32.lib -o fg_cuda_scenario_f.exe
if errorlevel 1 ( echo NVCC_SCENARIOF_FAILED & popd & exit /b 1 )
echo NVCC_CUDA_BUILDS_OK
popd
