#include "stdafx.h"
#include "FTPServer.h"
#include "Shared.h"
#include <d3dx9.h>
#include <xboxmath.h>
#include <imgui.h>
#include "imgui_impl_dx9.h"
#include "imgui_impl_xbox360.h"

static HANDLE g_exitThread = NULL;
static IDirect3DDevice9 *g_dev = NULL;

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

static bool CreateD3DDevice() {
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;

    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    HRESULT hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev);
    if (FAILED(hr)) {
        pp.EnableAutoDepthStencil = FALSE;
        hr = d3d->CreateDevice(0, D3DDEVTYPE_HAL, NULL,
            D3DCREATE_BUFFER_2_FRAMES | D3DCREATE_CREATE_THREAD_ON_0, &pp, &g_dev);
    }

    d3d->Release();
    return SUCCEEDED(hr) && g_dev != NULL;
}

void __cdecl main() {
    MountAllDrives();
    LogAdd("XeFTP starting...");
    CFTPServer::Instance().Start(21);

    if (!CreateD3DDevice()) {
        LogAdd("Failed to create D3D9 device");
        goto wait_exit;
    }

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplXbox360_Init()) {
        LogAdd("ImGui_ImplXbox360_Init failed");
        goto shutdown_d3d;
    }
    if (!ImGui_ImplDX9_Init(g_dev)) {
        LogAdd("ImGui_ImplDX9_Init failed");
        ImGui_ImplXbox360_Shutdown();
        goto shutdown_d3d;
    }

    g_exitThread = CreateThread(NULL, 0, ExitMonitor, NULL, 0, NULL);

    while (g_running) {
        ImGui_ImplXbox360_NewFrame();
        ImGui_ImplDX9_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::Begin("XeFTP", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse);

        ImGui::Text("XeFTP - Xbox 360 FTP Server");

        ImGui::Separator();
        ImGui::Text("Status");
        ImGui::Text("  IP:  %s :%d", g_ip.c_str(), g_port);
        ImGui::Text("  Connections:  %d", g_connCount);
        ImGui::Text("  Write Protect:  %s", g_writeProtect ? "ON" : "OFF");

        ImGui::Separator();
        ImGui::Text("Available Drives");
        int dcount = GetDriveCount();
        for (int i = 0; i < dcount; i++) {
            const char *mnt = GetDriveMountPoint(i);
            if (mnt && CheckDriveExists(mnt))
                ImGui::Text("  %s", mnt);
        }

        ImGui::Separator();
        ImGui::Text("Controls");
        ImGui::Text("  [X]  Toggle Write Protect");
        ImGui::Text("  BACK+START  Exit");
        ImGui::Text("  [D-Pad] Navigate");

        ImGui::Separator();
        int cnt = LogCount();
        if (cnt > 0) {
            ImGui::Text("Log");
            int start = 0;
            if (cnt > 10) start = cnt - 10;
            for (int i = start; i < cnt; i++)
                ImGui::Text("%s", LogGet(i));
        }

        ImGui::End();

        ImGui::Render();

        g_dev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 0, 0);
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        g_dev->Present(NULL, NULL, NULL, NULL);
    }

    ImGui_ImplDX9_Shutdown();
    ImGui_ImplXbox360_Shutdown();
shutdown_d3d:
    ImGui::DestroyContext();
    if (g_dev) { g_dev->Release(); g_dev = NULL; }
wait_exit:
    CFTPServer::Instance().Stop();
    if (g_exitThread) {
        WaitForSingleObject(g_exitThread, 1000);
        CloseHandle(g_exitThread);
    }
}
