#include "stdafx.h"
#include "FTPServer.h"
#include "Shared.h"

static HANDLE g_exitThread = NULL;

static DWORD WINAPI ExitMonitor(LPVOID arg) {
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(state));
    while (g_running) {
        XInputGetState(0, &state);
        if (state.dwPacketNumber) {
            if (state.Gamepad.wButtons & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START)) {
                g_running = 0;
                break;
            }
            if (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) {
                g_writeProtect = !g_writeProtect;
                LogAdd("Write protect toggled: %s", g_writeProtect ? "ON" : "OFF");
            }
        }
        Sleep(250);
    }
    return 0;
}

void __cdecl main() {
    MountAllDrives();

    LogAdd("XeFTP starting...");
    CFTPServer::Instance().Start(21);

    g_exitThread = CreateThread(NULL, 0, ExitMonitor, NULL, 0, NULL);

    while (g_running) {
        Sleep(100);
    }

    CFTPServer::Instance().Stop();
    if (g_exitThread) {
        WaitForSingleObject(g_exitThread, 1000);
        CloseHandle(g_exitThread);
    }
}
