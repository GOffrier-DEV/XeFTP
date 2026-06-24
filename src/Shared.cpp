#include "stdafx.h"
#include "Shared.h"

static CRITICAL_SECTION g_logLock;
static char g_log[LOG_LINES][LOG_LINE_LEN];
static int g_logIdx = 0;
static int g_logTotal = 0;
static bool g_logInit = false;

int g_running = 1;
int g_writeProtect = 1;
string g_ip = "0.0.0.0";
int g_port = 21;
int g_connCount = 0;

void LogAdd(const char *fmt, ...) {
    if (!g_logInit) {
        InitializeCriticalSection(&g_logLock);
        ZeroMemory(g_log, sizeof(g_log));
        g_logInit = true;
    }
    EnterCriticalSection(&g_logLock);
    va_list args;
    va_start(args, fmt);
    vsnprintf_s(g_log[g_logIdx], LOG_LINE_LEN, _TRUNCATE, fmt, args);
    va_end(args);
    g_logIdx = (g_logIdx + 1) % LOG_LINES;
    g_logTotal++;
    LeaveCriticalSection(&g_logLock);
}

const char* LogGet(int i) {
    return g_log[(g_logIdx + i) % LOG_LINES];
}

int LogCount(void) {
    return g_logTotal < LOG_LINES ? g_logTotal : LOG_LINES;
}

string PathToWin(string p) {
    if (p.empty()) return "";
    if (p[0] == '/') p = p.substr(1);
    size_t pos;
    while ((pos = p.find('/')) != string::npos) p.replace(pos, 1, "\\");
    if (p.find(':') == string::npos) {
        pos = p.find('\\');
        if (pos != string::npos) p.insert(pos, ":");
        else p += ":";
    }
    return p;
}

string PathToFtp(string p) {
    size_t pos;
    while ((pos = p.find('\\')) != string::npos) p.replace(pos, 1, "/");
    while ((pos = p.find(':')) != string::npos) p.erase(pos, 1);
    if (p[0] != '/') p = "/" + p;
    return p;
}

struct DriveEntry {
    const char *mountPoint;
    const char *devicePath;
};

static const DriveEntry g_driveTable[] = {
    {"flash:", "\\Device\\Flash"},
    {"memunit0:", "\\Device\\Mu0"},
    {"memunit1:", "\\Device\\Mu1"},
    {"onboardmu:", "\\Device\\BuiltInMuSfc"},
    {"dvd:", "\\Device\\Cdrom0"},
    {"hdd1:", "\\Device\\Harddisk0\\Partition1"},
    {"hdd0:", "\\Device\\Harddisk0\\Partition0"},
    {"hddx:", "\\Device\\Harddisk0\\SystemPartition"},
    {"sysext:", "\\Device\\Harddisk0\\SystemExtPartition"},
    {"usb0:", "\\Device\\Mass0"},
    {"usb1:", "\\Device\\Mass1"},
    {"usb2:", "\\Device\\Mass2"},
    {"hddvdplayer:", "\\Device\\HdDvdPlayer"},
    {"hddvdstorage:", "\\Device\\HdDvdStorage"},
    {"transfercable:", "\\Device\\Transfercable"},
    {"usbmu0:", "\\Device\\Mass0PartitionFile\\Storage"},
    {"usbmu1:", "\\Device\\Mass1PartitionFile\\Storage"},
    {"usbmu2:", "\\Device\\Mass2PartitionFile\\Storage"},
    {"usbmucache0:", "\\Device\\Mass0PartitionFile\\StorageSystem"},
    {"usbmucache1:", "\\Device\\Mass1PartitionFile\\StorageSystem"},
    {"usbmucache2:", "\\Device\\Mass2PartitionFile\\StorageSystem"},
    {"game:", ""},  // pre-mounted by system, skip in MountAllDrives
};

static const int g_driveCount = sizeof(g_driveTable) / sizeof(g_driveTable[0]);

int GetDriveCount() { return g_driveCount; }

const char* GetDriveMountPoint(int i) {
    if (i < 0 || i >= g_driveCount) return NULL;
    return g_driveTable[i].mountPoint;
}

bool CheckDriveExists(const char *driveWithColon) {
    // Try the given name, lowercase, and capital-first-letter variants
    string path = string(driveWithColon) + "\\";
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return true;

    string lower = driveWithColon;
    for (size_t i = 0; i < lower.size(); i++) lower[i] = (char)tolower(lower[i]);
    path = lower + "\\";
    attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return true;

    string cap = lower;
    if (!cap.empty()) cap[0] = (char)toupper(cap[0]);
    path = cap + "\\";
    attr = GetFileAttributesA(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return true;

    return false;
}

void MountAllDrives() {
    for (int i = 0; i < g_driveCount; i++) {
        // Skip Game: - it's already mounted by the system
        if (_stricmp(g_driveTable[i].mountPoint, "game:") == 0) continue;

        char mountConv[260];
        sprintf_s(mountConv, "\\??\\%s", g_driveTable[i].mountPoint);
        char sysPath[260];
        sprintf_s(sysPath, "%s", g_driveTable[i].devicePath);

        STRING sSysPath = { (USHORT)strlen(sysPath), (USHORT)strlen(sysPath) + 1, sysPath };
        STRING sMountConv = { (USHORT)strlen(mountConv), (USHORT)strlen(mountConv) + 1, mountConv };

        int res = ObCreateSymbolicLink(&sMountConv, &sSysPath);
        if (res == 0) {
            LogAdd("Mounted %s", g_driveTable[i].mountPoint);
        }
    }
}
