#pragma once
#include "stdafx.h"

#define XFER_BUF_SIZE (1024*1024)

class CFTPServerConn {
private:
    bool m_loggedIn;
    bool m_gotUser;
    bool m_passive;
    bool m_active;
    bool m_abortFlag;
    HANDLE m_thread;
    SOCKET m_passiveSocket;
    SOCKET m_dataSocket;
    int m_passivePort;
    ULONGLONG m_restPos;
    sockaddr_in m_xferAddr;
    vector<string> m_curPath;
    string m_renameFrom;

    string GetFullPath(const char *arg);
    void SendReply(const char *fmt, ...);
    int CreatePassiveSocket();
    SOCKET AcceptOrConnect();
    void CloseDataSocket();
    void ListDirectory(SOCKET s);
    string FormatFileLine(const string &name, bool isDir, DWORD size);
    char m_xferBuf[XFER_BUF_SIZE];

public:
    SOCKET m_cmdSocket;
    string m_xboxip;

    CFTPServerConn();
    ~CFTPServerConn();
    void Start();
    static DWORD WINAPI ThreadProc(LPVOID arg);
    DWORD Run();
};
