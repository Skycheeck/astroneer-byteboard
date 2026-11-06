// Byteboard in-game overlay. The Byteboard Lua mod loads this DLL (<mod>/Byteboard.dll) into
// ASTRONEER. On start it extracts the game data from the game's pak when needed (gamedata.cpp), then
// hooks IDXGISwapChain::Present and draws a Dear ImGui overlay: the research catalog (F8 or the
// backpack catalog) and the list of an opened printer. It talks to the Lua mod only through files
// in <mod>/in and <mod>/out (docs/PROTOCOL.md).
//
// Threads: Present runs on the game's render thread and does all ImGui and D3D work (render.cpp).
// The window procedure runs on the game thread; it queues input and decides what reaches the game
// (hooks.cpp). The DLL's own thread, MainThread below, extracts the data and then polls the mod's
// files (bridge.cpp).
// Shared state is atomic or behind a mutex. A fault inside an overlay frame turns the overlay off
// for the session (render.cpp, HkPresent).
#include <windows.h>

#include <cwchar>
#include <exception>
#include <string>

#include "bridge.h"
#include "catalog.h"
#include "gamedata.h"
#include "hooks.h"
#include "paths.h"
#include "util.h"

namespace rd {

namespace {

constexpr DWORD kPollIntervalMs = 100;
constexpr unsigned kStatePollEveryTicks = 5;  // state.json and results.json every 500 ms
constexpr double kPollErrorLogIntervalSec = 60;

HMODULE g_self = nullptr;

// One overlay per game process: later copies loaded by a Lua hot reload stay inert. The mutex
// handle stays open for the life of the process.
bool ClaimProcess() {
    wchar_t name[64];
    swprintf_s(name, L"Local\\Byteboard_%lu", GetCurrentProcessId());
    CreateMutexW(nullptr, FALSE, name);
    return GetLastError() != ERROR_ALREADY_EXISTS;
}

[[noreturn]] void PollForever() {
    RateLimitedLog errors(kPollErrorLogIntervalSec);
    for (unsigned tick = 0;; ++tick) {
        // A bad file or a failed read must not end the thread (built with /EHa, so this also
        // catches access violations).
        try {
            PollSignals();
            if (tick % kStatePollEveryTicks == 0) PollState();
        } catch (const std::exception& e) {
            errors.Write(std::string("poll error: ") + e.what());
        } catch (...) {
            errors.Write("poll error: structured exception");
        }
        Sleep(kPollIntervalMs);
    }
}

DWORD WINAPI MainThread(LPVOID) {
    if (!ClaimProcess()) return 0;
    if (!InitPaths(g_self)) return 0;  // not in a Byteboard mod folder: stay off
    Log("Byteboard " RD_VERSION " started");
    // Without data the overlay is pointless: when the catalog does not load, nothing is hooked.
    bool loaded = false;
    try {
        loaded = EnsureGameData(g_self, false) && LoadCatalog();
        // Data that does not load (a damaged file) is extracted once more, so it cannot keep the
        // overlay off on every start.
        if (!loaded && Paths().inGame) {
            Log("the game data does not load, extracting it again");
            loaded = EnsureGameData(g_self, true) && LoadCatalog();
        }
    } catch (...) {
        loaded = false;
    }
    if (!loaded) {
        Log("catalog load failed, overlay stays off");
        return 0;
    }
    InstallHooks(g_self);
    PollForever();
}

}  // namespace

}  // namespace rd

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        rd::g_self = module;
        DisableThreadLibraryCalls(module);
        // Pin the module: a UE4SS hot reload (Ctrl+R) closes the Lua state, whose package.loadlib
        // finalizer calls FreeLibrary. Unloading would leave the Present/WndProc/IAT hooks pointing
        // into freed code and crash the game.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)&DllMain, &pinned);
        // The Lua mod writes into these folders as soon as this load returns.
        rd::CreateModFolders(module);
        if (HANDLE thread = CreateThread(nullptr, 0, rd::MainThread, nullptr, 0, nullptr)) CloseHandle(thread);
    }
    return TRUE;
}
