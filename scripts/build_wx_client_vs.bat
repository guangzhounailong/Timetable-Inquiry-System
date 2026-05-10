@echo off
setlocal

set "WXWIN=E:\wxWidgets"
set "WX_LIB_DIR=%WXWIN%\lib\vc_x64_lib"
set "WX_SETUP_DIR=%WX_LIB_DIR%\mswu"
set "PROJECT_DIR=%~dp0.."
set "SRC_DIR=%PROJECT_DIR%\src"
set "INCLUDE_DIR=%PROJECT_DIR%\include"
set "BIN_DIR=%PROJECT_DIR%\bin"
set "BUILD_DIR=%PROJECT_DIR%\build"
set "VS18_VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

if exist "%VS18_VCVARS%" (
    call "%VS18_VCVARS%"
)

if not exist "%WXWIN%\include\wx\wx.h" (
    echo wxWidgets headers were not found at "%WXWIN%".
    echo Edit WXWIN in this script if your wxWidgets path is different.
    exit /b 1
)

if not exist "%WX_SETUP_DIR%\wx\setup.h" (
    echo wxWidgets release setup.h was not found.
    echo Build wxWidgets first with:
    echo.
    echo   cd /d "%WXWIN%\build\msw"
    echo   nmake /f makefile.vc BUILD=release SHARED=0 UNICODE=1 TARGET_CPU=X64 RUNTIME_LIBS=dynamic USE_STC=0 CXXFLAGS="/utf-8 /std:c++17"
    echo.
    exit /b 1
)

if not exist "%WX_LIB_DIR%\wxmsw33u_core.lib" (
    echo wxWidgets release libraries were not found.
    echo Build wxWidgets first with:
    echo.
    echo   cd /d "%WXWIN%\build\msw"
    echo   nmake /f makefile.vc BUILD=release SHARED=0 UNICODE=1 TARGET_CPU=X64 RUNTIME_LIBS=dynamic USE_STC=0 CXXFLAGS="/utf-8 /std:c++17"
    echo.
    exit /b 1
)

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cd /d "%PROJECT_DIR%"

rc /nologo ^
    /i "%WXWIN%\include" /i "%WX_SETUP_DIR%" ^
    /fo "%BUILD_DIR%\wx_client.res" ^
    "%SRC_DIR%\wx_client.rc"
if errorlevel 1 exit /b 1

cl /EHsc /std:c++17 /utf-8 /MD /D__WXMSW__ /D_UNICODE /DUNICODE ^
    /I"%INCLUDE_DIR%" /I"%WXWIN%\include" /I"%WX_SETUP_DIR%" ^
    "%SRC_DIR%\wx_client.cpp" "%SRC_DIR%\SecureChannel.cpp" /Fo"%BUILD_DIR%\\" /Fe"%BIN_DIR%\wx_client.exe" ^
    /link /SUBSYSTEM:WINDOWS /LIBPATH:"%WX_LIB_DIR%" ^
    "%BUILD_DIR%\wx_client.res" ^
    wxmsw33u_core.lib wxbase33u.lib ^
    wxpng.lib wxjpeg.lib wxtiff.lib wxzlib.lib wxregexu.lib wxexpat.lib ^
    Ws2_32.lib bcrypt.lib comctl32.lib rpcrt4.lib winmm.lib advapi32.lib shell32.lib ^
    ole32.lib oleaut32.lib uuid.lib comdlg32.lib gdi32.lib user32.lib ^
    winspool.lib oleacc.lib uxtheme.lib version.lib gdiplus.lib msimg32.lib ^
    shlwapi.lib imm32.lib wininet.lib msvcprt.lib vcruntime.lib ucrt.lib

endlocal
