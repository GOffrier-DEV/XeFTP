#pragma once

extern volatile int g_running;
extern string g_ip;
extern int g_port;
extern int g_connCount;

// Drive mounting (from FSD)
void MountAllDrives();

// Drive table info - iterate available drives
int GetDriveCount();
const char* GetDriveMountPoint(int i);
bool CheckDriveExists(const char *driveWithColon);
