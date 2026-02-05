@echo off
setlocal

echo ========================================
echo VDI Client - Build and Package
echo ========================================
echo.

REM Set Qt path (modify according to your installation)
set QT_PATH=C:\Qt\6.10.2\mingw_64

echo [Check] Qt path: %QT_PATH%
if not exist "%QT_PATH%" (
    echo [ERROR] Qt path not found: %QT_PATH%
    echo Please modify QT_PATH variable in this script
    pause
    exit /b 1
)

set WINDEPLOYQT=%QT_PATH%\bin\windeployqt.exe
echo [Check] windeployqt path: %WINDEPLOYQT%
if not exist "%WINDEPLOYQT%" (
    echo [ERROR] windeployqt not found: %WINDEPLOYQT%
    pause
    exit /b 1
)

REM Detect Inno Setup
set ISCC=
if exist "D:\Inno Setup 6\ISCC.exe" set ISCC=D:\Inno Setup 6\ISCC.exe
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" set ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe
if exist "C:\Program Files\Inno Setup 6\ISCC.exe" set ISCC=C:\Program Files\Inno Setup 6\ISCC.exe

if "%ISCC%"=="" (
    echo [ERROR] Inno Setup not found
    echo Please download from https://jrsoftware.org/isdl.php
    pause
    exit /b 1
)

echo [Check] Inno Setup path: %ISCC%
echo.

echo [1/5] Cleaning old build files...
if exist "build" rmdir /s /q "build"
mkdir build
echo Done.

echo.
echo [2/5] Configuring CMake project...
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo [ERROR] CMake configuration failed
    cd ..
    pause
    exit /b 1
)
echo Done.

echo.
echo [3/5] Building project...
cmake --build . --config Release
if errorlevel 1 (
    echo [ERROR] Build failed
    cd ..
    pause
    exit /b 1
)
echo Done.

echo.
echo [4/5]... Deploying Qt dependencies...
echo Using windeployqt to deploy all Qt libraries...
"%WINDEPLOYQT%" --release --no-translations VDIClient.exe
if errorlevel 1 (
    echo [ERROR] Qt deployment failed
    cd ..
    pause
    exit /b 1
)
echo Done.

cd ..

echo.
echo [5/5] Creating installer package...
echo ========================================

if not exist "installer.iss" (
    echo [ERROR] installer installer.iss not found
    pause
    exit /b 1
)

"%ISCC%" installer.iss
if errorlevel 1 (
    echo [ERROR] Installer creation failed
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build and package completed!
echo ========================================
echo.
echo Installer location: installer_output\VDIClient-Setup.exe
echo.
echo You can now run the installer for testing.
echo.

pause
