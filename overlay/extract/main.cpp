// byteboard-extract.exe <pak> <outDir>: runs the game data extraction of the overlay DLL on its own
// (development and checks). Prints the log lines; exit code 0 on success, 1 on failure.
#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <cstdio>
#include <string>

#include "extract.h"

namespace {

// UTF-8 text to a console (as UTF-16, whatever its code page) or, redirected, as bytes.
void Write(DWORD stream, const std::string& text) {
    const HANDLE handle = GetStdHandle(stream);
    DWORD mode = 0;
    DWORD written = 0;
    if (GetConsoleMode(handle, &mode)) {
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), wide.data(), length);
        WriteConsoleW(handle, wide.data(), (DWORD)wide.size(), &written, nullptr);
    } else {
        WriteFile(handle, text.data(), (DWORD)text.size(), &written, nullptr);
    }
}

void PrintLine(const std::string& line) {
    Write(STD_OUTPUT_HANDLE, line + "\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        Write(STD_ERROR_HANDLE, "usage: byteboard-extract.exe <pak> <outDir>\n");
        return 1;
    }
    const auto start = std::chrono::steady_clock::now();
    const rd::extract::Result result = rd::extract::ExtractGameData(argv[1], argv[2], PrintLine);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    PROCESS_MEMORY_COUNTERS memory{};
    GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof memory);
    char summary[160];
    snprintf(summary, sizeof summary, "%.2f s, peak memory %.0f MB", seconds, memory.PeakPagefileUsage / 1048576.0);
    if (!result.ok) {
        Write(STD_ERROR_HANDLE, "error: " + result.error + "\n");
        PrintLine(std::string("failed after ") + summary);
        return 1;
    }
    PrintLine("extracted " + std::to_string(result.items) + " items and " + std::to_string(result.icons) +
              " icons in " + summary);
    return 0;
}
