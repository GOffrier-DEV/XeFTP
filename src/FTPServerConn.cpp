#include "stdafx.h"
#include "FTPServerConn.h"
#include "Auth.h"
#include "Shared.h"

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#endif

CFTPServerConn::CFTPServerConn()
    : m_loggedIn(false), m_gotUser(false), m_passive(true), m_active(false),
      m_thread(NULL), m_cmdSocket(INVALID_SOCKET), m_passiveSocket(INVALID_SOCKET),
      m_passivePort(0) {
    ZeroMemory(&m_xferAddr, sizeof(m_xferAddr));
}

CFTPServerConn::~CFTPServerConn() {
    if (m_cmdSocket != INVALID_SOCKET) closesocket(m_cmdSocket);
    if (m_passiveSocket != INVALID_SOCKET) closesocket(m_passiveSocket);
}

void CFTPServerConn::Start() {
    m_thread = CreateThread(NULL, 0, ThreadProc, this, 0, NULL);
}

DWORD WINAPI CFTPServerConn::ThreadProc(LPVOID arg) {
    CFTPServerConn *self = (CFTPServerConn*)arg;
    DWORD ret = self->Run();
    InterlockedDecrement((LONG*)&g_connCount);
    delete self;
    return ret;
}

void CFTPServerConn::SendReply(const char *fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    strcat_s(buf, "\r\n");
    send(m_cmdSocket, buf, (int)strlen(buf), 0);
}

int CFTPServerConn::CreatePassiveSocket() {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return -1;

    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, 0x5802, (PCSTR)&on, sizeof(BOOL));
    setsockopt(s, SOL_SOCKET, 0x5801, (PCSTR)&on, sizeof(BOOL));

    sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(0);

    if (bind(s, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }

    int len = sizeof(local);
    getsockname(s, (sockaddr*)&local, &len);
    m_passivePort = ntohs(local.sin_port);

    if (listen(s, 1) == SOCKET_ERROR) {
        closesocket(s);
        return -1;
    }
    return (int)s;
}

SOCKET CFTPServerConn::AcceptOrConnect() {
    if (m_passive) {
        return accept(m_passiveSocket, NULL, NULL);
    } else {
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) return INVALID_SOCKET;
        BOOL on = TRUE;
        setsockopt(s, SOL_SOCKET, 0x5802, (PCSTR)&on, sizeof(BOOL));
        setsockopt(s, SOL_SOCKET, 0x5801, (PCSTR)&on, sizeof(BOOL));
        if (connect(s, (sockaddr*)&m_xferAddr, sizeof(m_xferAddr)) < 0) {
            closesocket(s);
            return INVALID_SOCKET;
        }
        return s;
    }
}

string CFTPServerConn::GetFullPath(const char *arg) {
    string s = arg;
    // Convert forward slashes to backslashes
    size_t pos;
    while ((pos = s.find('/')) != string::npos) s.replace(pos, 1, "\\");

    if (s.empty() || s == "\\") {
        // No path or root - just return current path
    } else if (s.find('\\') == string::npos && s.find(':') == string::npos) {
        string cur = m_curPath[0];
        if (!cur.empty() && cur[cur.size()-1] == ':')
            cur = cur.substr(0, cur.size()-1);
        cur += ":\\";
        for (size_t i = 1; i < m_curPath.size(); i++) {
            cur += m_curPath[i];
            cur += "\\";
        }
        s = cur + s;
    } else {
        // Convert to Win32 path
        if (s[0] == '\\') s = s.substr(1);
        if (s.find(':') == string::npos) {
            pos = s.find('\\');
            if (pos != string::npos) s.insert(pos, ":");
            else s += ":";
        }
    }
    return s;
}

string CFTPServerConn::FormatFileLine(const string &name, bool isDir, DWORD size) {
    char buf[256];
    char dir = isDir ? 'd' : '-';
    sprintf_s(buf, "%crwxr-xr-x   1 root root    %d Jan 01  2000 %s\r\n", dir, size, name.c_str());
    return string(buf);
}

static DWORD CheckDriveAttr(const char *name) {
    // Try given case first, then lowercase, then capital first letter
    string path = string(name) + ":\\";
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return attr;
    // Try all lowercase
    string lower = name;
    for (size_t i = 0; i < lower.size(); i++) lower[i] = (char)tolower(lower[i]);
    path = lower + ":\\";
    attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return attr;
    // Try capital first letter
    string cap = lower;
    if (!cap.empty()) cap[0] = (char)toupper(cap[0]);
    path = cap + ":\\";
    attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return attr;
    return INVALID_FILE_ATTRIBUTES;
}

void CFTPServerConn::ListDirectory(SOCKET s) {
    if (m_curPath.empty()) {
        // Method 1: enumerate \\* for mount points
        WIN32_FIND_DATAA ffd;
        HANDLE h = FindFirstFileA("\\*", &ffd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                string name = ffd.cFileName;
                if (name == "." || name == "..") continue;
                bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                string line = FormatFileLine(name + ":", isDir, ffd.nFileSizeLow);
                send(s, line.c_str(), (int)line.length(), 0);
            } while (FindNextFileA(h, &ffd));
            FindClose(h);
            return;
        }

        // Method 2: try known drive names with case variants
        const char *driveNames[] = {"hdd1", "hddx", "hdd", "usb0", "flash", "dvd", "game", "mu", NULL};
        for (int i = 0; driveNames[i]; i++) {
            DWORD attr = CheckDriveAttr(driveNames[i]);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                string line = FormatFileLine(string(driveNames[i]) + ":", true, 0);
                send(s, line.c_str(), (int)line.length(), 0);
            }
        }
    } else {
        string path = m_curPath[0];
        // Strip trailing colon if present (CWD may store it with :)
        if (!path.empty() && path[path.size()-1] == ':')
            path = path.substr(0, path.size()-1);
        path += ":\\";
        for (size_t i = 1; i < m_curPath.size(); i++) {
            path += m_curPath[i];
            path += "\\";
        }
        WIN32_FIND_DATAA ffd;
        string search = path + "*";
        HANDLE h = FindFirstFileA(search.c_str(), &ffd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(ffd.cFileName, ".") == 0) continue;
                bool isDir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                string line = FormatFileLine(ffd.cFileName, isDir, ffd.nFileSizeLow);
                send(s, line.c_str(), (int)line.length(), 0);
            } while (FindNextFileA(h, &ffd));
            FindClose(h);
        }
    }
}

DWORD CFTPServerConn::Run() {
    m_active = true;
    LogAdd("FTP client connected");

    SendReply("220 XeFTP ready");

    char cmdBuf[512];
    char argBuf[512];

    while (m_active && g_running) {
        // Read command
        fd_set fds;
        TIMEVAL tv;
        tv.tv_sec = 180;
        tv.tv_usec = 0;

        FD_ZERO(&fds);
        FD_SET(m_cmdSocket, &fds);
        int sel = select(0, &fds, NULL, NULL, &tv);
        if (sel <= 0) {
            SendReply("221 timeout");
            break;
        }

        int pos = 0;
        ZeroMemory(cmdBuf, sizeof(cmdBuf));
        while (pos < (int)sizeof(cmdBuf) - 1) {
            char c;
            int n = recv(m_cmdSocket, &c, 1, 0);
            if (n <= 0) { m_active = false; break; }
            if (c == '\r') continue;
            if (c == '\n') break;
            cmdBuf[pos++] = c;
        }
        if (!m_active) break;
        cmdBuf[pos] = 0;

        // Parse command
        char cmd[6] = {0};
        argBuf[0] = 0;
        int i = 0;
        while (i < 5 && cmdBuf[i] && cmdBuf[i] != ' ') {
            cmd[i] = (char)toupper(cmdBuf[i]);
            i++;
        }
        cmd[i] = 0;
        if (cmdBuf[i] == ' ') {
            strncpy_s(argBuf, cmdBuf + i + 1, _TRUNCATE);
        }

        // Handle command
        if (strcmp(cmd, "USER") == 0) {
            m_gotUser = true;
            SendReply("331 User ok, need password");
        } else if (strcmp(cmd, "PASS") == 0) {
            if (m_gotUser && CAuth::Check("xbox", argBuf)) {
                m_loggedIn = true;
                SendReply("230 Login successful");
            } else {
                SendReply("530 Login incorrect");
            }
        } else if (strcmp(cmd, "QUIT") == 0) {
            SendReply("221 Bye");
            break;
        } else if (strcmp(cmd, "SYST") == 0) {
            SendReply("215 XeFTP");
        } else if (strcmp(cmd, "FEAT") == 0) {
            SendReply("211-Extensions:");
            SendReply(" SIZE");
            SendReply("211 End");
        } else if (strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
            string p = "";
            for (size_t i = 0; i < m_curPath.size(); i++) {
                p += m_curPath[i];
                if (i > 0) p += "/";
            }
            if (p.empty()) p = "/";
            else p = "/" + p;
            SendReply("257 \"%s\"", p.c_str());
        } else if (strcmp(cmd, "TYPE") == 0) {
            SendReply("200 Type set to I");
        } else if (strcmp(cmd, "MODE") == 0) {
            SendReply("200 Mode set to S");
        } else if (strcmp(cmd, "STRU") == 0) {
            SendReply("200 Structure set to F");
        } else if (strcmp(cmd, "NOOP") == 0) {
            SendReply("200 OK");
        } else if (strcmp(cmd, "PASV") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            if (m_passiveSocket != INVALID_SOCKET) closesocket(m_passiveSocket);
            int ps = CreatePassiveSocket();
            if (ps < 0) {
                SendReply("425 Can't open passive connection");
                continue;
            }
            m_passiveSocket = (SOCKET)ps;
            sockaddr_in local;
            int len = sizeof(local);
            getsockname(m_passiveSocket, (sockaddr*)&local, &len);
            m_passivePort = ntohs(local.sin_port);
            char rep[256];
            string ip = m_xboxip;
            for (size_t i = 0; i < ip.length(); i++)
                if (ip[i] == '.') ip[i] = ',';
            sprintf_s(rep, "227 Entering Passive Mode (%s,%d,%d)",
                ip.c_str(), m_passivePort >> 8, m_passivePort & 0xFF);
            SendReply("%s", rep);
            m_passive = true;
        } else if (strcmp(cmd, "PORT") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            int h1, h2, h3, h4, p1, p2;
            sscanf_s(argBuf, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2);
            char *a = (char*)&m_xferAddr.sin_addr;
            char *p = (char*)&m_xferAddr.sin_port;
            a[0] = (char)h1; a[1] = (char)h2; a[2] = (char)h3; a[3] = (char)h4;
            p[0] = (char)p1; p[1] = (char)p2;
            m_xferAddr.sin_family = AF_INET;
            m_passive = false;
            SendReply("200 PORT ok");
        } else if (strcmp(cmd, "LIST") == 0 || strcmp(cmd, "NLST") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            SendReply("150 Opening data connection");
            SOCKET ds = AcceptOrConnect();
            if (ds == INVALID_SOCKET) {
                SendReply("425 Can't open data connection");
                continue;
            }
            ListDirectory(ds);
            closesocket(ds);
            SendReply("226 Transfer complete");
        } else if (strcmp(cmd, "RETR") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string fp = GetFullPath(argBuf);
            FILE *f = NULL;
            fopen_s(&f, fp.c_str(), "rb");
            if (!f) {
                SendReply("550 File not found");
                continue;
            }
            SendReply("150 Opening BINARY data connection");
            SOCKET ds = AcceptOrConnect();
            if (ds == INVALID_SOCKET) {
                fclose(f);
                SendReply("425 Can't open data connection");
                continue;
            }
            int n;
            while ((n = (int)fread(m_xferBuf, 1, XFER_BUF_SIZE, f)) > 0) {
                int sent = 0;
                while (sent < n) {
                    int r = send(ds, m_xferBuf + sent, n - sent, 0);
                    if (r <= 0) break;
                    sent += r;
                }
            }
            fclose(f);
            closesocket(ds);
            SendReply("226 Transfer complete");
        } else if (strcmp(cmd, "STOR") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            if (g_writeProtect) { SendReply("550 Write protected"); continue; }
            string fp = GetFullPath(argBuf);
            SendReply("150 Opening BINARY data connection");
            SOCKET ds = AcceptOrConnect();
            if (ds == INVALID_SOCKET) {
                SendReply("425 Can't open data connection");
                continue;
            }
            FILE *f = NULL;
            fopen_s(&f, fp.c_str(), "wb");
            if (!f) {
                closesocket(ds);
                SendReply("550 Can't create file");
                continue;
            }
            int n;
            while ((n = recv(ds, m_xferBuf, XFER_BUF_SIZE, 0)) > 0) {
                fwrite(m_xferBuf, 1, n, f);
            }
            fclose(f);
            closesocket(ds);
            SendReply("226 Transfer complete");
        } else if (strcmp(cmd, "DELE") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            if (g_writeProtect) { SendReply("550 Write protected"); continue; }
            string fp = GetFullPath(argBuf);
            if (DeleteFileA(fp.c_str()))
                SendReply("250 File deleted");
            else
                SendReply("550 Delete failed");
        } else if (strcmp(cmd, "RMD") == 0 || strcmp(cmd, "XRMD") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            if (g_writeProtect) { SendReply("550 Write protected"); continue; }
            string fp = GetFullPath(argBuf);
            if (RemoveDirectoryA(fp.c_str()))
                SendReply("250 Directory removed");
            else
                SendReply("550 Remove failed");
        } else if (strcmp(cmd, "MKD") == 0 || strcmp(cmd, "XMKD") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            if (g_writeProtect) { SendReply("550 Write protected"); continue; }
            string fp = GetFullPath(argBuf);
            if (CreateDirectoryA(fp.c_str(), NULL))
                SendReply("257 Directory created");
            else
                SendReply("550 Create failed");
        } else if (strcmp(cmd, "CWD") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string arg = argBuf;
            if (arg.empty() || arg == "/") {
                m_curPath.clear();
                SendReply("250 OK");
            } else if (arg == "..") {
                if (!m_curPath.empty()) m_curPath.pop_back();
                SendReply("250 OK");
            } else {
                // Replace / with \, add : for drive
                size_t p;
                while ((p = arg.find('/')) != string::npos) arg[p] = '\\';
                if (m_curPath.empty() && arg.find(':') == string::npos) arg += ":";
                // Check if the path exists
                string testPath = "";
                if (m_curPath.empty()) {
                    testPath = arg + "\\";
                } else {
                    string base = m_curPath[0];
                    if (!base.empty() && base[base.size()-1] == ':')
                        base = base.substr(0, base.size()-1);
                    testPath = base + ":\\";
                    for (size_t i = 1; i < m_curPath.size(); i++) {
                        testPath += m_curPath[i];
                        testPath += "\\";
                    }
                    testPath += arg + "\\";
                }
                DWORD attr = GetFileAttributesA(testPath.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    // Split arg into parts and push
                    string part = "";
                    for (size_t i = 0; i < arg.length(); i++) {
                        if (arg[i] == '\\') {
                            if (!part.empty()) {
                                if (m_curPath.empty() && part.find(':') == string::npos) part += ":";
                                m_curPath.push_back(part);
                                part = "";
                            }
                        } else {
                            part += arg[i];
                        }
                    }
                    if (!part.empty()) {
                        if (m_curPath.empty() && part.find(':') == string::npos) part += ":";
                        m_curPath.push_back(part);
                    }
                    SendReply("250 OK");
                } else {
                    SendReply("550 Path not found");
                }
            }
        } else if (strcmp(cmd, "SIZE") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string fp = GetFullPath(argBuf);
            HANDLE h = CreateFileA(fp.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (h == INVALID_HANDLE_VALUE) {
                SendReply("550 File not found");
            } else {
                LARGE_INTEGER sz;
                GetFileSizeEx(h, &sz);
                CloseHandle(h);
                SendReply("213 %I64d", sz.QuadPart);
            }
        } else if (strcmp(cmd, "CDUP") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            if (!m_curPath.empty()) m_curPath.pop_back();
            SendReply("250 OK");
        } else if (strcmp(cmd, "SITE") == 0) {
            if (_stricmp(argBuf, "WRITEPROTECT") == 0) {
                g_writeProtect = !g_writeProtect;
                SendReply("200 Write protect %s", g_writeProtect ? "ON" : "OFF");
            } else {
                SendReply("500 Unknown SITE command");
            }
        } else {
            SendReply("500 Unknown command");
        }
    }

    closesocket(m_cmdSocket);
    closesocket(m_passiveSocket);
    m_cmdSocket = INVALID_SOCKET;
    m_passiveSocket = INVALID_SOCKET;
    m_active = false;
    return 0;
}
