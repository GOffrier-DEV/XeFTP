#include "stdafx.h"
#include "FTPServer.h"
#include "FTPServerConn.h"
#include "Shared.h"

CFTPServer::CFTPServer() : m_listener(INVALID_SOCKET), m_thread(NULL) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    XNADDR xna;
    XNetGetTitleXnAddr(&xna);
    char ip[16];
    sprintf_s(ip, 16, "%d.%d.%d.%d",
        (BYTE)xna.ina.S_un.S_un_b.s_b1,
        (BYTE)xna.ina.S_un.S_un_b.s_b2,
        (BYTE)xna.ina.S_un.S_un_b.s_b3,
        (BYTE)xna.ina.S_un.S_un_b.s_b4);
    m_ip = ip;
    g_ip = ip;
}

bool CFTPServer::Start(int port) {
    m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listener == INVALID_SOCKET)
        return false;

    BOOL on = TRUE;
    setsockopt(m_listener, SOL_SOCKET, 0x5802, (PCSTR)&on, sizeof(BOOL));
    setsockopt(m_listener, SOL_SOCKET, 0x5801, (PCSTR)&on, sizeof(BOOL));

    sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons((u_short)port);

    if (bind(m_listener, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        closesocket(m_listener);
        return false;
    }

    if (listen(m_listener, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(m_listener);
        return false;
    }

    m_thread = CreateThread(NULL, 0, ListenerThread, this, 0, NULL);
    if (!m_thread) {
        closesocket(m_listener);
        return false;
    }

    return true;
}

DWORD WINAPI CFTPServer::ListenerThread(LPVOID arg) {
    CFTPServer *self = (CFTPServer*)arg;
    // SetThreadName not available on Xbox 360

    while (g_running) {
        sockaddr_in remote;
        int rlen = sizeof(remote);
        SOCKET c = accept(self->m_listener, (sockaddr*)&remote, &rlen);
        if (c == INVALID_SOCKET) {
            continue;
        }
        InterlockedIncrement((LONG*)&g_connCount);
        CFTPServerConn *conn = new CFTPServerConn();
        conn->m_cmdSocket = c;
        conn->m_xboxip = self->m_ip;
        conn->Start();
    }
    return 0;
}

void CFTPServer::Stop() {
    g_running = 0;
    if (m_listener != INVALID_SOCKET) {
        closesocket(m_listener);
        m_listener = INVALID_SOCKET;
    }
    if (m_thread) {
        WaitForSingleObject(m_thread, 3000);
        CloseHandle(m_thread);
        m_thread = NULL;
    }
}

string CFTPServer::GetIP() {
    return m_ip;
}
