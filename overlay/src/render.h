#pragma once
// The overlay's frame: ImGui setup on the first Present, input from the window procedure's queue,
// the view state machine, drawing into the game's back buffer, and the fault guard around it all.
// Threads: the game's render thread (inside IDXGISwapChain::Present). SetFrameHooks is called by
// preview.exe before its first frame, from the thread that presents.
#include <dxgi.h>

namespace rd {

// The Present hook installed by InstallHooks. A fault inside an overlay frame turns the overlay off
// for the rest of the session; the game keeps running.
HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);

// Extension points for preview_capture.cpp, which only ResearchOverlayPreview.dll contains.
struct FrameHooks {
    bool persistLayout = true;                            // false: no overlay.ini, the player's layout stays untouched
    void (*afterFrame)(IDXGISwapChain* swapChain) = nullptr;  // called after each drawn overlay frame
};
void SetFrameHooks(const FrameHooks& hooks);

}  // namespace rd
