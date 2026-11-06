#pragma once
// Hooks into the game: IDXGISwapChain::Present (vtable patch), the game window's procedure
// (subclassing) and SetCursorPos/ClipCursor (import table patches), plus the state they share.
// Threads: InstallHooks runs on the overlay's MainThread. The window procedure and the cursor hooks
// run on the game thread, Present on the render thread; they share only atomics and the input queue.
#include <windows.h>

#include <dxgi.h>

#include <string>
#include <vector>

namespace rd {

// Installs the cursor and Present hooks and logs each result.
void InstallHooks(HMODULE self);
HRESULT CallOriginalPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);

// Render thread: subclasses the game window once ImGui is ready.
void SubclassGameWindow(HWND hwnd);
// True when another hook replaced the game window's procedure after ours.
bool WindowProcReplaced(HWND hwnd);

// Render thread: what the window procedure and the cursor hooks need to know about the view.
// escapeClosesView: Esc hides the overlay's view instead of going to the game.
void PublishView(bool visible, bool escapeClosesView);
void PublishWants(bool mouse, bool keyboard);
// Render thread: true once after the window procedure swallowed an Esc meant for the overlay.
bool TakeEscapeRequest();

// Turns the overlay off for the rest of the session: hidden, and every hook passes through.
// Any thread.
void DisableOverlay(const std::string& reason);
bool OverlayDisabled();

// A window message queued for ImGui, with the modifier keys as the game thread saw them when the
// message arrived (GetKeyState is per thread and means nothing on the render thread).
struct InputEvent {
    UINT message = 0;
    WPARAM wParam = 0;
    LPARAM lParam = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};
// Render thread: the events queued since the last call.
std::vector<InputEvent> TakeInputEvents();

}  // namespace rd
