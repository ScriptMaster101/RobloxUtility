@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

set OUT_DIR=..\
set SOURCES=main.cpp chrome.cpp firefox.cpp media.cpp paths.cpp stub.cpp sqlite.cpp

REM --- clean ---
if /I "%~1"=="clean" (
    del /Q "%OUT_DIR%utility.dll" 2>nul
    del /Q "%OUT_DIR%utility.exp" 2>nul
    del /Q "%OUT_DIR%utility.lib" 2>nul
    del /Q "%OUT_DIR%*.obj" 2>nul
    exit /b 0
)

REM --- pick compiler ---
set USE_COMPILER=%~1
if "%USE_COMPILER%"=="" (
    where cl.exe >nul 2>&1
    if !ERRORLEVEL!==0 (set USE_COMPILER=msvc) else (
        where g++.exe >nul 2>&1
        if !ERRORLEVEL!==0 (set USE_COMPILER=mingw) else (
            echo [!] No compiler found. Use "x64 Native Tools Command Prompt for VS 2022".
            exit /b 1
        )
    )
)

REM --- build ---
if /I "%USE_COMPILER%"=="msvc" (
    where cl.exe >nul 2>&1
    if !ERRORLEVEL! neq 0 (
        set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
        if exist "!VSWHERE!" (
            for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do set "VS_PATH=%%i"
            if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
                call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
            )
        )
    )
    cl.exe /nologo /LD /EHsc /O2 /MT /std:c++17 /D UTILITY_EXPORTS /D NDEBUG ^
        /Fe:"%OUT_DIR%utility.dll" %SOURCES% ^
        /link ws2_32.lib crypt32.lib iphlpapi.lib shell32.lib advapi32.lib bcrypt.lib ole32.lib oleaut32.lib wininet.lib ^
        /OPT:REF /OPT:ICF
    if !ERRORLEVEL! neq 0 (echo [!] build FAILED & exit /b 1)
    echo [+] %OUT_DIR%utility.dll
    goto :done
)

if /I "%USE_COMPILER%"=="mingw" (
    g++.exe -shared -o "%OUT_DIR%utility.dll" -std=c++17 -O2 -s ^
        -D UTILITY_EXPORTS -D NDEBUG ^
        -static-libgcc -static-libstdc++ %SOURCES% ^
        -lws2_32 -lcrypt32 -liphlpapi -lshell32 -ladvapi32 -lbcrypt -lole32 -loleaut32 -lwininet
    if !ERRORLEVEL! neq 0 (echo [!] build FAILED & exit /b 1)
    echo [+] %OUT_DIR%utility.dll
    goto :done
)

echo [!] unknown compiler: %USE_COMPILER%
exit /b 1

:done
dir "%OUT_DIR%utility.dll" 2>nul
exit /b 0
