#include "stdafx.h"
#include "FTPServerConn.h"
#include "Auth.h"
#include "Shared.h"

CFTPServerConn::CFTPServerConn()
    : m_loggedIn(false), m_gotUser(false), m_passive(true), m_active(false),
      m_abortFlag(false), m_thread(NULL), m_cmdSocket(INVALID_SOCKET),
      m_passiveSocket(INVALID_SOCKET), m_dataSocket(INVALID_SOCKET),
      m_passivePort(0), m_restPos(0) {
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
    m_abortFlag = false;
    if (m_passive) {
        m_dataSocket = accept(m_passiveSocket, NULL, NULL);
        return m_dataSocket;
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
        m_dataSocket = s;
        return s;
    }
}

void CFTPServerConn::CloseDataSocket() {
    if (m_dataSocket != INVALID_SOCKET) {
        closesocket(m_dataSocket);
        m_dataSocket = INVALID_SOCKET;
    }
    if (m_passiveSocket != INVALID_SOCKET) {
        closesocket(m_passiveSocket);
        m_passiveSocket = INVALID_SOCKET;
    }
}

static bool IsWriteAllowed(const string &path) {
    string low = path;
    for (size_t i = 0; i < low.size(); i++) low[i] = (char)tolower(low[i]);
    if (low.find("flash:") == 0) return false;
    return true;
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
        if (s[0] == '\\') s = s.substr(1);
        if (s.find(':') == string::npos) {
            pos = s.find('\\');
            if (pos != string::npos) s.insert(pos, ":");
            else s += ":";
        }
        pos = s.find(':');
        if (pos != string::npos && pos + 1 < s.size() && s[pos + 1] != '\\')
            s.insert(pos + 1, "\\");
    }
    return s;
}

string CFTPServerConn::FormatFileLine(const string &name, bool isDir, DWORD size) {
    char buf[256];
    char dir = isDir ? 'd' : '-';
    sprintf_s(buf, "%crwxr-xr-x   1 root root    %d Jan 01  2000 %s\r\n", dir, size, name.c_str());
    return string(buf);
}

void CFTPServerConn::ListDirectory(SOCKET s) {
    if (m_curPath.empty()) {
        // Iterate our mount table and list drives that exist
        int count = GetDriveCount();
        for (int i = 0; i < count; i++) {
            const char *mount = GetDriveMountPoint(i);
            if (!mount) continue;
            // Skip the dummy game: entry - game: already shows from system mount
            if (_stricmp(mount, "game:") == 0 && GetFileAttributesA("game:\\") != INVALID_FILE_ATTRIBUTES) {
                string line = FormatFileLine(mount, true, 0);
                send(s, line.c_str(), (int)line.length(), 0);
                continue;
            }
            if (CheckDriveExists(mount)) {
                string line = FormatFileLine(mount, true, 0);
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
            SendReply(" MDTM");
            SendReply(" REST STREAM");
            SendReply(" APPE");
            SendReply(" STOU");
            SendReply(" RNFR");
            SendReply(" RNTO");
            SendReply("211 End");
        } else if (strcmp(cmd, "PWD") == 0 || strcmp(cmd, "XPWD") == 0) {
            string p;
            for (size_t i = 0; i < m_curPath.size(); i++) {
                string seg = m_curPath[i];
                if (!seg.empty() && seg[seg.size()-1] == ':')
                    seg = seg.substr(0, seg.size()-1);
                if (!p.empty()) p += "/";
                p += seg;
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
            CloseDataSocket();
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
            if (m_restPos > 0) fseek(f, (long)m_restPos, SEEK_SET);
            m_restPos = 0;
            SendReply("150 Opening BINARY data connection");
            SOCKET ds = AcceptOrConnect();
            if (ds == INVALID_SOCKET) {
                fclose(f);
                SendReply("425 Can't open data connection");
                continue;
            }
            int n;
            while ((n = (int)fread(m_xferBuf, 1, XFER_BUF_SIZE, f)) > 0) {
                if (m_abortFlag) break;
                int sent = 0;
                while (sent < n) {
                    int r = send(ds, m_xferBuf + sent, n - sent, 0);
                    if (r <= 0) break;
                    sent += r;
                }
            }
            fclose(f);
            CloseDataSocket();
            SendReply("226 Transfer complete");
        } else if (strcmp(cmd, "STOR") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string fp = GetFullPath(argBuf);
            if (!IsWriteAllowed(fp)) { SendReply("550 Write protected"); continue; }
            SendReply("150 Opening BINARY data connection");
            SOCKET ds = AcceptOrConnect();
            if (ds == INVALID_SOCKET) {
                SendReply("425 Can't open data connection");
                continue;
            }
            FILE *f = NULL;
            const char *mode = (m_restPos > 0) ? "ab" : "wb";
            fopen_s(&f, fp.c_str(), mode);
            if (!f) {
                CloseDataSocket();
                SendReply("550 Can't create file");
                continue;
            }
            if (m_restPos > 0) fseek(f, (long)m_restPos, SEEK_SET);
            m_restPos = 0;
            int n;
            while ((n = recv(ds, m_xferBuf, XFER_BUF_SIZE, 0)) > 0) {
                if (m_abortFlag) break;
                fwrite(m_xferBuf, 1, n, f);
            }
            fclose(f);
            CloseDataSocket();
            SendReply("226 Transfer complete");
        } else if (strcmp(cmd, "APPE") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string fp = GetFullPath(argBuf);
            if (!IsWriteAllowed(fp)) { SendReply("550 Write protected"); continue; }
            SOCKET ds = AcceptOrConnect();
            if (ds == INVALID_SOCKET) {
                SendReply("425 Can't open data connection");
                continue;
            }
            FILE *f = NULL;
            fopen_s(&f, fp.c_str(), "ab");
            if (!f) {
                CloseDataSocket();
                SendReply("550 Can't open file");
                continue;
            }
            SendReply("150 Opening BINARY data connection");
            int n;
            while ((n = recv(ds, m_xferBuf, XFER_BUF_SIZE, 0)) > 0) {
                if (m_abortFlag) break;
                fwrite(m_xferBuf, 1, n, f);
            }
            fclose(f);
            CloseDataSocket();
            SendReply("226 Transfer complete");
        } else if (strcmp(cmd, "DELE") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string fp = GetFullPath(argBuf);
            if (!IsWriteAllowed(fp)) { SendReply("550 Write protected"); continue; }
            if (DeleteFileA(fp.c_str()))
                SendReply("250 File deleted");
            else
                SendReply("550 Delete failed");
        } else if (strcmp(cmd, "RMD") == 0 || strcmp(cmd, "XRMD") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string fp = GetFullPath(argBuf);
            if (!IsWriteAllowed(fp)) { SendReply("550 Write protected"); continue; }
            if (RemoveDirectoryA(fp.c_str()))
                SendReply("250 Directory removed");
            else
                SendReply("550 Remove failed");
        } else if (strcmp(cmd, "MKD") == 0 || strcmp(cmd, "XMKD") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string fp = GetFullPath(argBuf);
            if (!IsWriteAllowed(fp)) { SendReply("550 Write protected"); continue; }
            if (CreateDirectoryA(fp.c_str(), NULL))
                SendReply("257 Directory created");
            else
                SendReply("550 Create failed");
        } else if (strcmp(cmd, "CWD") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            string arg = argBuf;
            if (arg.empty() || arg == "/" || arg == "\\") {
                m_curPath.clear();
                SendReply("250 OK");
            } else if (arg == "..") {
                if (!m_curPath.empty()) m_curPath.pop_back();
                SendReply("250 OK");
            } else {
                // Normalize: / -> \, strip leading root slash
                bool absolute = (arg[0] == '/');
                size_t p;
                while ((p = arg.find('/')) != string::npos) arg[p] = '\\';
                if (arg[0] == '\\') { absolute = true; arg = arg.substr(1); }
                if (arg.empty()) { m_curPath.clear(); SendReply("250 OK"); continue; }

                // Absolute (has colon) vs relative
                bool hasColon = (arg.find(':') != string::npos);
                bool hasSep = (arg.find('\\') != string::npos);

                // Absolute paths missing colon — e.g. "hdd1\Content" from CWD /hdd1/Content
                while (absolute && !hasColon) {
                    size_t sc = arg.find('\\');
                    if (sc == string::npos) {
                        // single component: "hdd1" -> "hdd1:"
                        arg += ":";
                    } else {
                        // "hdd1\Content" -> "hdd1:\Content"
                        arg.insert(sc, ":");
                    }
                    hasColon = true;
                }
                // For first component without colon when m_curPath is empty, add colon
                if (!hasColon && m_curPath.empty()) arg += ":";

                // Check the target path exists
                bool pathOk = false;
                if (hasColon) {
                    {
                        size_t cc = arg.find(':');
                        if (cc != string::npos && cc + 1 < arg.size() && arg[cc + 1] != '\\')
                            arg.insert(cc + 1, "\\");
                    }
                    string testPath = arg + "\\";
                    DWORD attr = GetFileAttributesA(testPath.c_str());
                    pathOk = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
                    if (pathOk) m_curPath.clear();
                } else if (m_curPath.empty() && !hasSep) {
                    // Drive-only: match against mount table
                    string drive = arg;
                    if (drive[drive.size()-1] != ':') drive += ":";
                    int count = GetDriveCount();
                    for (int i = 0; i < count; i++) {
                        const char *mount = GetDriveMountPoint(i);
                        if (mount && _stricmp(mount, drive.c_str()) == 0) {
                            pathOk = CheckDriveExists(mount);
                            break;
                        }
                    }
                } else {
                    // Relative path: append to current
                    string testPath;
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
                    pathOk = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
                }

                if (!pathOk) {
                    SendReply("550 Path not found");
                    continue;
                }

                // Build m_curPath from arg parts
                if (hasColon) m_curPath.clear();
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
        } else if (strcmp(cmd, "RNFR") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            m_renameFrom = GetFullPath(argBuf);
            SendReply("350 Ready for destination name");
        } else if (strcmp(cmd, "RNTO") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            if (m_renameFrom.empty()) {
                SendReply("503 Need RNFR first");
                continue;
            }
            string fp = GetFullPath(argBuf);
            if (MoveFileA(m_renameFrom.c_str(), fp.c_str()))
                SendReply("250 Rename OK");
            else
                SendReply("550 Rename failed");
            m_renameFrom.clear();
        } else if (strcmp(cmd, "REST") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            m_restPos = _strtoui64(argBuf, NULL, 10);
            SendReply("350 Restart at %I64u", m_restPos);
        } else if (strcmp(cmd, "ABOR") == 0) {
            m_abortFlag = true;
            CloseDataSocket();
            SendReply("226 ABOR OK");
        } else if (strcmp(cmd, "STAT") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            if (argBuf[0] == 0) {
                string p;
                for (size_t i = 0; i < m_curPath.size(); i++) {
                    string seg = m_curPath[i];
                    if (!seg.empty() && seg[seg.size()-1] == ':')
                        seg = seg.substr(0, seg.size()-1);
                    if (!p.empty()) p += "/";
                    p += seg;
                }
                if (p.empty()) p = "/";
                else p = "/" + p;
                SendReply("211-XeFTP status:");
                SendReply(" Logged in: yes");
                SendReply(" Current path: %s", p.c_str());
                SendReply(" Flash: read-only");
                SendReply("211 End");
            } else {
                string fp = GetFullPath(argBuf);
                HANDLE h = CreateFileA(fp.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
                if (h == INVALID_HANDLE_VALUE) {
                    SendReply("550 File not found");
                } else {
                    LARGE_INTEGER sz;
                    GetFileSizeEx(h, &sz);
                    CloseHandle(h);
                    SendReply("213 %I64u", sz.QuadPart);
                }
            }
        } else if (strcmp(cmd, "HELP") == 0) {
            SendReply("214-The following commands are supported:");
            SendReply(" USER PASS QUIT SYST FEAT PWD TYPE MODE STRU");
            SendReply(" NOOP PASV PORT LIST NLST RETR STOR APPE STOU");
            SendReply(" DELE RMD MKD CWD CDUP PWD SIZE RNFR RNTO");
            SendReply(" REST ABOR STAT HELP SITE REIN ACCT ALLO");
            SendReply("214 End");
        } else if (strcmp(cmd, "STOU") == 0) {
            if (!m_loggedIn) { SendReply("530 Not logged in"); continue; }
            static unsigned int g_stouCounter = 0;
            char num[16];
            sprintf_s(num, "%u", ++g_stouCounter);
            string name = string("XeFTP_") + num;
            string fp = GetFullPath(name.c_str());
            SOCKET ds = AcceptOrConnect();
            if (ds == INVALID_SOCKET) {
                SendReply("425 Can't open data connection");
                continue;
            }
            FILE *f = NULL;
            fopen_s(&f, fp.c_str(), "wb");
            if (!f) {
                CloseDataSocket();
                SendReply("550 Can't create file");
                continue;
            }
            SendReply("150 Opening BINARY data connection for %s", name.c_str());
            int n;
            while ((n = recv(ds, m_xferBuf, XFER_BUF_SIZE, 0)) > 0) {
                if (m_abortFlag) break;
                fwrite(m_xferBuf, 1, n, f);
            }
            fclose(f);
            CloseDataSocket();
            SendReply("226 Transfer complete, %s", name.c_str());
        } else if (strcmp(cmd, "ALLO") == 0) {
            SendReply("202 Allocate OK");
        } else if (strcmp(cmd, "REIN") == 0) {
            m_loggedIn = false;
            m_gotUser = false;
            m_curPath.clear();
            m_renameFrom.clear();
            m_restPos = 0;
            CloseDataSocket();
            SendReply("220 Service ready for new user");
        } else if (strcmp(cmd, "ACCT") == 0) {
            SendReply("230 Account OK");
        } else if (strcmp(cmd, "SITE") == 0) {
            SendReply("500 Unknown SITE command");
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
