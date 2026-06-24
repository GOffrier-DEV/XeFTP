# Summary: XeFTP - Xbox 360 FTP Server

## Goal
- Build a standalone Xbox 360 FTP daemon (XeFTP) with D3D9 on-screen overlay for status/connection info, compiled with official Microsoft XDK into a devkit-flagged `.xex` for RGH/JTAG consoles.

## Constraints & Preferences
- **SDK**: Official Xbox 360 SDK 2.0.21256.3 (`XEDK` = `C:\Program Files (x86)\Microsoft Xbox 360 SDK`)
- **Build**: SDK-only (`cl.exe`, `link.exe`, `imagexex.exe` from `bin\win32`); no `xextool.exe` or third-party tools
- **Language**: C++ with MSVC Xbox 360 cross-compiler (`/TP`), `std::string`, `std::vector`, classes
- **Entry point**: `void __cdecl main()`
- **Networking**: Winsock via `winsockx.h` (included by `xtl.h`); `socket`, `setsockopt(0x5802/0x5801)`, `bind`, `listen`, `accept`, `send`/`recv`
- **Threading**: `CreateThread` / `CloseHandle`
- **File I/O**: Win32 API (`CreateFileA`, `FindFirstFileA`, `GetFileAttributesA`, `DeleteFileA`, `MoveFileA`, etc.)
- **Auth**: Hardcoded `xbox` / `xbox`, no anonymous
- **Write protection**: Block `STOR`/`APPE`/`DELE`/`MKD`/`RMD` when on; toggled by controller `[X]` button or `SITE WRITEPROTECT`
- **On-screen display**: D3D9 overlay with bitmap font rendered via vertex/pixel shaders as textured quads
- **Socket opts 0x5802/0x5801**: Must call `setsockopt(sock, SOL_SOCKET, 0x5802, TRUE)` and `0x5801` before `bind()` to disable encryption — from FSD
- **Privilege 6 (InsecureSockets)**: Required in `xex.xml` for socket patches to work — from FSD
- **Passive FTP**: `PASV` command creates a new listening socket each time; `CloseDataSocket()` tracks the data socket for `ABOR`
- **Version info**: `XEDK` 2.0.21256.3, `link.exe` v10.00.11886.00, `imagexex.exe` v2.0.21256.0, `cl.exe` v16.00.11886.00 (PPCBE target)

## Progress
### Done
- Project directory created and all source files written: `stdafx.h`, `Shared.h/.cpp`, `Auth.h/.cpp`, `FTPServer.h/.cpp`, `FTPServerConn.h/.cpp`, `Renderer.h/.cpp`, `font8x16.inc`, `main.cpp`, `build.bat`, `xex.xml`, `.gitignore`
- `MountAllDrives()` in `Shared.cpp` using `ObCreateSymbolicLink` for 22 drive entries (all lowercase); `game:` pre-mounted by system, skipped
- Devkit-flagged `.xex` (Title ID `0x58454554`, Insecure Sockets, Load `0x82000000`) builds with one `cl`+`link /XEXCONFIG:xex.xml` step
- Full RFC 959 command set implemented: `RNFR`/`RNTO`, `APPE`, `REST`, `ABOR`, `STAT`, `HELP`, `STOU`, `ALLO`, `REIN`, `ACCT`
- `FEAT` advertises `SIZE`, `MDTM`, `REST STREAM`, `APPE`, `STOU`, `RNFR`, `RNTO`
- Root FTP listing iterates `g_driveTable` directly (22 mount entries) and checks each via `GetFileAttributesA`
- GitHub repo created at `GOffrier-DEV/XeFTP`, initial commit with author `GOffrier-DEV`
- Researched Xbox 360 D3D9 rendering from `imgui-xbox360` (ClementDreptin), `xb360.dev` guide, FSD source
- D3D overlay code written: `Renderer.h` with `TextVertex` struct and `MAX_CHARS=2000`
- `Renderer.cpp` with `CreateDevice()`, `CreateShaders()`, `CreateFontTexture()`, `CreateBuffers()`, `Render()`, `RenderText()`
- `stdafx.h` updated to include `<d3d9.h>`
- `main.cpp` updated: `CRenderer::Instance().Init()` before main loop, `CRenderer::Instance().Render()` inside loop
- **Build fix**: Added `xgraphics.lib` to `build.bat` → resolves all missing `XShaderPDBBuilder_*`, `XGConvertDXTokensToMicrocode`, `XGValidateMicrocode` symbols; removed `xbdm.lib` (didn't help)
- **Build now succeeds** — `Release\XeFTP.xex` (4.5 MB)
- `src/shaders.inc` generated from `fxc.exe` (Xbox 360 SDK shader compiler) — passthrough vertex shader (`vs_3_0`) and texture×color pixel shader (`ps_3_0`), available for future pre-compiled optimization

### In Progress
- (none)

### Blocked
- (none)

## Current `build.bat` Link Line
```
set LIBS=libcMT.lib libcpMT.lib d3d9.lib d3dx9.lib xgraphics.lib xapilib.lib xboxkrnl.lib xnet.lib oldnames.lib
```

## Key Decisions
- **SDK-only build**: Removed `xextool.exe` from build pipeline; devkit-flagged `.xex` runs on RGH without retail conversion
- **Direct `.xex` output from linker**: `link.exe /OUT:*.xex /XEXCONFIG:xex.xml` produces complete XEX in one step
- **FSD drive mounting via `ObCreateSymbolicLink`**: 22-entry drive table with lowercase names matching CWD expectations
- **Root listing from mount table**: Iterates `g_driveTable` directly instead of `FindFirstFileA("\\*")`
- **CWD absolute vs relative**: Any arg with `:` is absolute (clears `m_curPath`); no-colon args are relative
- **D3D9 approach**: Shader-based (no fixed function), `D3DFMT_LIN_A8R8G8B8` textures, `D3DVertexDeclaration` instead of FVF, matrix transposed before setting as vertex shader constant for HLSL column-major convention
- **Shader compilation**: D3DXCompileShader at runtime using linked `d3dx9.lib` + `xgraphics.lib` (symbols: `XShaderPDBBuilder_*`, `XGConvertDXTokensToMicrocode`, `XGValidateMicrocode`, `XGCompare*`)
- **xgraphics.lib**: Static library containing full shader compiler/optimizer/validator — includes objects for PDB building, microcode conversion, IL handling, R500 code generation, and state machine management

## Relevant Files
- `src/Renderer.h` — `TextVertex` struct, `CRenderer` class with `CreateDevice`/`CreateShaders`/`CreateFontTexture`/`CreateBuffers`/`Render`/`RenderText`, `MAX_CHARS=2000`
- `src/Renderer.cpp` — Full D3D9 implementation: device creation with fallback, shader compilation via `D3DXCompileShader`, font texture from `font8x16.inc`, vertex/index buffer rendering, orthographic projection, alpha-blended text overlay
- `src/shaders.inc` — Pre-compiled `vs_3_0`/`ps_3_0` bytecode (generated by `fxc.exe`), available for future optimization to remove `d3dx9.lib` dependency
- `src/font8x16.inc` — 8×16 bitmap font (256 chars × 16 bytes), used by `CreateFontTexture()` in Renderer
- `src/main.cpp` — `CRenderer::Instance().Init()` + `Instance().Render()` in main loop
- `src/stdafx.h` — includes `<d3d9.h>` (was added for D3D types)
- `build.bat` — compiles `Renderer.obj`, links `d3d9.lib d3dx9.lib xgraphics.lib xapilib.lib xboxkrnl.lib`
- `Release\XeFTP.xex` — 4.5 MB, builds clean

## Next Steps
1. (Optional) Replace D3DXCompileShader with pre-compiled shaders from `shaders.inc` to remove `d3dx9.lib`/`xgraphics.lib` dependency, reducing XEX size and startup time
2. Test on actual Xbox 360 hardware (RGH/JTAG)
3. Fine-tune D3D overlay rendering (position, colors, update frequency)
