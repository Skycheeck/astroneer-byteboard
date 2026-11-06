#include "util.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <mutex>

#include "paths.h"

namespace rd {

namespace {

// Larger files are not ours: the biggest one (icons.rgba) is about 1 MB.
constexpr long long kMaxReadBytes = 64ll * 1024 * 1024;
constexpr double kFileTimeTicksPerSec = 1e7;

class HandleCloser {
public:
    explicit HandleCloser(HANDLE h) : h_(h) {}
    ~HandleCloser() { CloseHandle(h_); }
    HandleCloser(const HandleCloser&) = delete;
    HandleCloser& operator=(const HandleCloser&) = delete;

private:
    HANDLE h_;
};

double SecondsSince(const FILETIME& t) {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a{}, b{};
    a.LowPart = now.dwLowDateTime;
    a.HighPart = now.dwHighDateTime;
    b.LowPart = t.dwLowDateTime;
    b.HighPart = t.dwHighDateTime;
    return ((double)a.QuadPart - (double)b.QuadPart) / kFileTimeTicksPerSec;
}

}  // namespace

std::wstring Widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Utf8(const fs::path& p) {
    return Narrow(p.wstring());
}

std::wstring Lower(std::wstring w) {
    if (!w.empty()) CharLowerBuffW(w.data(), (DWORD)w.size());
    std::replace(w.begin(), w.end(), L'ё', L'е');
    return w;
}

std::string Upper(const std::string& s) {
    std::wstring w = Widen(s);
    if (!w.empty()) CharUpperBuffW(w.data(), (DWORD)w.size());
    return Narrow(w);
}

std::string AsciiLower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return s;
}

std::string_view Trim(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> Split(std::string_view s, char separator) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    for (size_t at = s.find(separator); at != std::string_view::npos; at = s.find(separator, start)) {
        parts.push_back(s.substr(start, at - start));
        start = at + 1;
    }
    parts.push_back(s.substr(start));
    return parts;
}

std::string FormatInt(long long v) {
    const std::string digits = std::to_string(v < 0 ? -v : v);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0) out += "\xC2\xA0";
        out += digits[i];
    }
    return v < 0 ? "-" + out : out;
}

std::string ObjectName(std::string_view classPath) {
    const size_t dot = classPath.rfind('.');
    std::string_view name = dot == std::string_view::npos ? classPath : classPath.substr(dot + 1);
    if (name.size() > 2 && name.substr(name.size() - 2) == "_C") name.remove_suffix(2);
    return std::string(name);
}

std::string ClassKey(std::string_view classPath) {
    return AsciiLower(ObjectName(Trim(classPath)));
}

bool ReadSharedFile(const fs::path& path, std::string& data, double* ageSec) {
    data.clear();
    const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    HandleCloser closer(h);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > kMaxReadBytes) return false;
    data.resize((size_t)size.QuadPart);
    size_t done = 0;
    while (done < data.size()) {
        DWORD got = 0;
        if (!::ReadFile(h, data.data() + done, (DWORD)(data.size() - done), &got, nullptr)) return false;
        if (got == 0) break;  // the file shrank while being read
        done += got;
    }
    data.resize(done);
    if (ageSec) {
        FILETIME written{};
        *ageSec = GetFileTime(h, nullptr, nullptr, &written) ? SecondsSince(written) : 1e9;
    }
    return true;
}

bool FileExists(const fs::path& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void Log(const std::string& line) {
    static std::mutex mutex;
    try {
        const fs::path& out = Paths().out;
        if (out.empty()) return;
        std::lock_guard lock(mutex);
        SYSTEMTIME t;
        GetLocalTime(&t);
        char stamp[32];
        snprintf(stamp, sizeof stamp, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
        std::ofstream(out / "overlay.log", std::ios::app) << stamp << line << "\n";
    } catch (...) {
    }
}

void RateLimitedLog::Write(const std::string& line) {
    const auto now = std::chrono::steady_clock::now();
    if (written_ && std::chrono::duration<double>(now - lastAt_).count() < intervalSec_) {
        ++suppressed_;
        return;
    }
    if (suppressed_ > 0) Log(line + " (and " + std::to_string(suppressed_) + " more since the last report)");
    else Log(line);
    written_ = true;
    lastAt_ = now;
    suppressed_ = 0;
}

}  // namespace rd
