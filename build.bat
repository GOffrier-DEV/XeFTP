@echo off
set XEDK=C:\Program Files (x86)\Microsoft Xbox 360 SDK
set PATH=%XEDK%\bin\win32;%PATH%
set INCLUDE=%XEDK%\include\xbox;%XEDK%\include\xbox\sys;%XEDK%\include\win32
set LIB=%XEDK%\lib\xbox;%XEDK%\lib\win32

if "%1"=="" set CONFIG=Release
if "%1"=="debug" set CONFIG=Debug
if "%1"=="Debug" set CONFIG=Debug
if "%1"=="Release" set CONFIG=Release

set OUTDIR=%CONFIG%
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

set CFLAGS=/nologo /c /TP /GS- /GF- /GR- /W3 /D_XBOX /D_XBOX360 /DNDEBUG /DIMGUI_DISABLE_WIN32_FUNCTIONS /I"." /I"imgui" /I"imgui\examples\imgui"
set LIBS=libcMT.lib libcpMT.lib d3d9.lib d3dx9.lib xapilib.lib xboxkrnl.lib xnet.lib oldnames.lib xgraphics.lib

if "%CONFIG%"=="Debug" (
    set CFLAGS=/nologo /c /TP /GS- /GF- /GR- /W3 /D_XBOX /D_XBOX360 /D_DEBUG /Od /DIMGUI_DISABLE_WIN32_FUNCTIONS /I"." /I"imgui" /I"imgui\examples\imgui"
    set LIBS=libcMTD.lib libcpMTD.lib d3d9.lib d3dx9d.lib xapilibd.lib xboxkrnl.lib xnetd.lib oldnames.lib xgraphics.lib
)

echo === Compiling ===
cl %CFLAGS% /Fosrc\stdafx.obj src\stdafx.cpp /Ycstdafx.h /Fp%OUTDIR%\stdafx.pch
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fosrc\main.obj src\main.cpp /Yustdafx.h /Fp%OUTDIR%\stdafx.pch
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fosrc\Auth.obj src\Auth.cpp /Yustdafx.h /Fp%OUTDIR%\stdafx.pch
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fosrc\FTPServer.obj src\FTPServer.cpp /Yustdafx.h /Fp%OUTDIR%\stdafx.pch
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fosrc\FTPServerConn.obj src\FTPServerConn.cpp /Yustdafx.h /Fp%OUTDIR%\stdafx.pch
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fosrc\Shared.obj src\Shared.cpp /Yustdafx.h /Fp%OUTDIR%\stdafx.pch
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fo%OUTDIR%\imgui.obj imgui\examples\imgui\imgui.cpp
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fo%OUTDIR%\imgui_draw.obj imgui\examples\imgui\imgui_draw.cpp
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fo%OUTDIR%\imgui_widgets.obj imgui\examples\imgui\imgui_widgets.cpp
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fo%OUTDIR%\imgui_tables.obj imgui\examples\imgui\imgui_tables.cpp
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fo%OUTDIR%\imgui_impl_dx9.obj imgui\imgui_impl_dx9.cpp
if %ERRORLEVEL% NEQ 0 goto error

cl %CFLAGS% /Fo%OUTDIR%\imgui_impl_xbox360.obj imgui\imgui_impl_xbox360.cpp
if %ERRORLEVEL% NEQ 0 goto error

echo === Linking ===
link /nologo /NODEFAULTLIB:LIBCMT /NODEFAULTLIB:LIBCPMT /NODEFAULTLIB:LIBCI /NODEFAULTLIB:XAPI ^
    /OUT:%OUTDIR%\XeFTP.xex /XEXCONFIG:xex.xml /DEBUG ^
    src\stdafx.obj src\main.obj src\Auth.obj src\FTPServer.obj ^
    src\FTPServerConn.obj src\Shared.obj ^
    %OUTDIR%\imgui.obj %OUTDIR%\imgui_draw.obj %OUTDIR%\imgui_widgets.obj %OUTDIR%\imgui_tables.obj ^
    %OUTDIR%\imgui_impl_dx9.obj %OUTDIR%\imgui_impl_xbox360.obj %LIBS%
if %ERRORLEVEL% NEQ 0 goto error

echo === Done: %OUTDIR%\XeFTP.xex ===
goto done

:error
echo BUILD FAILED
exit /b 1

:done
exit /b 0
