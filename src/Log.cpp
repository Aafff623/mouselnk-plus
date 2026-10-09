#include "Log.h"

#include <windows.h>

#include <mutex>

namespace logger {
namespace {

std::mutex   g_mutex;
HANDLE       g_file = INVALID_HANDLE_VALUE;   // 只在本模块的锁内使用，故用裸句柄
std::wstring g_path;
std::string  g_pathUtf8;
unsigned long long g_maxBytes = 512 * 1024;
unsigned long long g_written = 0;

std::string ToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (need <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), out.data(), need,
                          nullptr, nullptr);
    return out;
}

const char* LevelTag(Level level) {
    switch (level) {
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        default:           return "INFO ";
    }
}

// 打开（或重开）日志文件，追加写入。
bool OpenLocked() {
    ::CreateDirectoryW(std::filesystem::path(g_path).parent_path().c_str(), nullptr);
    g_file = ::CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(g_file, &size)) {
        g_written = static_cast<unsigned long long>(size.QuadPart);
    }
    return true;
}

// 轮转：把当前文件改名成 .1，并重开。只保留一份历史，避免日志把便携目录写爆。
void RotateLocked() {
    if (g_file != INVALID_HANDLE_VALUE) {
        ::CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    const std::wstring backup = g_path + L".1";
    ::DeleteFileW(backup.c_str());
    ::MoveFileW(g_path.c_str(), backup.c_str());
    g_written = 0;
    OpenLocked();
}

}  // namespace

void Init(const std::filesystem::path& file, unsigned long long maxBytes) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) {
        ::CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
    g_path = file.wstring();
    g_pathUtf8 = ToUtf8(g_path);
    g_maxBytes = maxBytes == 0 ? 512 * 1024 : maxBytes;
    g_written = 0;
    OpenLocked();
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) {
        ::CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

void Write(Level level, const std::string& utf8Message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (g_written >= g_maxBytes) {
        RotateLocked();
        if (g_file == INVALID_HANDLE_VALUE) {
            return;
        }
    }

    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    char line[2048]{};
    const int n = ::wsprintfA(line, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] ", st.wYear, st.wMonth,
                              st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                              LevelTag(level));
    if (n > 0) {
        std::string text(line, static_cast<size_t>(n));
        text += utf8Message;
        text += "\r\n";
        DWORD written = 0;
        ::WriteFile(g_file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        g_written += written;
    }
}

const std::string& Path() {
    return g_pathUtf8;
}

}  // namespace logger
