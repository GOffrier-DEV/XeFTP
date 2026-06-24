#pragma once

#define LOG_LINES 20
#define LOG_LINE_LEN 128

extern int g_running;
extern string g_ip;
extern int g_port;
extern int g_connCount;

void LogAdd(const char *fmt, ...);
const char* LogGet(int i);
int LogCount(void);
string PathToWin(string p);
string PathToFtp(string p);

// Drive mounting (from FSD)
void MountAllDrives();

// Drive table info - iterate available drives
int GetDriveCount();
const char* GetDriveMountPoint(int i);
bool CheckDriveExists(const char *driveWithColon);
