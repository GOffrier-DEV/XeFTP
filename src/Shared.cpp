#include "stdafx.h"
#include "Shared.h"

volatile int g_running = 1;
string g_ip = "0.0.0.0";
int g_port = 21;
int g_connCount = 0;

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

        ObCreateSymbolicLink(&sMountConv, &sSysPath);
    }
}
