#pragma once
// String, file and log helpers.
// Threads: any. The only shared state is the log file, which Log writes under a mutex.
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rd {

namespace fs = std::filesystem;

std::wstring Widen(std::string_view utf8);
std::string Narrow(std::wstring_view wide);
// UTF-8 for ImGui (font and ini paths); fs::path::string() would use the ANSI code page.
std::string Utf8(const fs::path& path);
// Lower case for search, with ё folded into е.
std::wstring Lower(std::wstring text);
std::string Upper(const std::string& utf8);
std::string AsciiLower(std::string text);
std::string_view Trim(std::string_view text);
std::vector<std::string_view> Split(std::string_view text, char separator);

// "12 500", with a no-break space as the thousands separator (Bahnschrift has no narrow one).
std::string FormatInt(long long value);

// The object name of a class path, which docs/PROTOCOL.md uses to compare classes: the part after
// the last '.', without a trailing "_C" ("/Game/A/Splitter_3Way.Splitter_3Way_C" -> "Splitter_3Way").
std::string ObjectName(std::string_view classPath);
// ObjectName in lower case. Unreal compares names case-insensitively, and one name can reach the
// game and the extracted catalog with different capitalisation.
std::string ClassKey(std::string_view classPath);

// Reads a whole file opened with full sharing (read, write and delete), so the mod can delete and
// replace it meanwhile. ageSec receives the seconds since the last write. False when the file
// cannot be opened or read.
bool ReadSharedFile(const fs::path& path, std::string& data, double* ageSec = nullptr);
bool FileExists(const fs::path& path);

// Appends a line to ue4ss/out/overlay.log. Never throws; does nothing before InitPaths.
void Log(const std::string& line);

// Logs the first line, then at most one line per interval, with the number of lines left out.
// Not thread-safe: each instance belongs to one thread.
class RateLimitedLog {
public:
    explicit RateLimitedLog(double intervalSec) : intervalSec_(intervalSec) {}
    void Write(const std::string& line);

private:
    double intervalSec_;
    bool written_ = false;
    std::chrono::steady_clock::time_point lastAt_;
    int suppressed_ = 0;
};

}  // namespace rd
