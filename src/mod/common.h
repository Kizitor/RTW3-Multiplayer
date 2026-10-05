#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <mutex>
#include <deque>
#include <functional>

#define MP_MOD_VERSION "0.2.0"
#define MP_PROTOCOL 2
#define MP_DEFAULT_PORT 47624

// Window messages used by the main-thread dispatcher.
#define WM_MP_NET (WM_APP + 0x311)
#define WM_MP_CALL (WM_APP + 0x312)

void LogInit(const std::wstring& dir);
void Log(const char* fmt, ...);

std::string W2U(const std::wstring& w);
std::wstring U2W(const std::string& s);
std::string Trim(const std::string& s);
std::vector<std::string> Split(const std::string& s, char sep);

bool ReadFileBytes(const std::wstring& path, std::string& out);
bool WriteFileBytes(const std::wstring& path, const std::string& data);
bool FileExists(const std::wstring& path);
void EnsureDir(const std::wstring& path);

// "<Documents>\My Games\Rule the Waves 3\RTW3MP\" (created on demand).
std::wstring ModDataDir();
std::wstring GameDir();

// Simple key=value text messages (one per line, values may not contain newlines).
typedef std::map<std::string, std::string> KV;
std::string KVEncode(const KV& kv);
KV KVDecode(const std::string& s);
int KVInt(const KV& kv, const char* key, int def = 0);
std::string KVStr(const KV& kv, const char* key, const std::string& def = "");

// Runs a function on the game's main (VCL) thread at the next message-loop iteration.
void RunOnMainThread(std::function<void()> fn);
DWORD MainThreadId();
HWND DispatcherHwnd();
