#pragma once
#include "stdafx.h"

class CFTPServer {
private:
    CFTPServer();
    ~CFTPServer() {}
    CFTPServer(const CFTPServer&);
    CFTPServer& operator=(const CFTPServer&);

    SOCKET m_listener;
    HANDLE m_thread;

    static DWORD WINAPI ListenerThread(LPVOID arg);

public:
    static CFTPServer& Instance() {
        static CFTPServer s;
        return s;
    }

    bool Start(int port);
    void Stop();
    string GetIP();
    string m_ip;
};
