#pragma once

#define LOG_LINES 20
#define LOG_LINE_LEN 128

extern volatile int g_running;
extern string g_ip;
extern int g_port;
extern int g_connCount;

void LogAdd(const char *fmt, ...);
const char* LogGet(int i);
int LogCount(void);

#define CMD_LOG_LINES 500
#define CMD_LOG_LINE_LEN 256
void CmdLogAdd(const char *fmt, ...);
const char* CmdLogGet(int i);
int CmdLogCount(void);
extern volatile int g_cmdLogTotal;

string PathToWin(string p);
string PathToFtp(string p);

// Drive mounting (from FSD)
void MountAllDrives();

// Drive table info - iterate available drives
int GetDriveCount();
const char* GetDriveMountPoint(int i);
bool CheckDriveExists(const char *driveWithColon);
