#include "hooks.h"

#include <d3d11.h>

#include <atomic>
#include <mutex>

#include "render.h"
#include "util.h"

namespace rd {

namespace {

// IDXGISwapChain's vtable: IUnknown (3 slots), IDXGIObject (4), IDXGIDeviceSubObject (1), then Present.
constexpr int kPresentVtableSlot = 8;
constexpr size_t kMaxQueuedInput = 512;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);

PresentFn g_origPresent = nullptr;
// The originals are set before the import table is patched, so the hooks never see null.
SetCursorPosFn g_origSetCursorPos = &SetCursorPos;
ClipCursorFn g_origClipCursor = &ClipCursor;
std::atomic<WNDPROC> g_origWndProc{nullptr};

std::atomic<bool> g_visible{false};
std::atomic<bool> g_escapeClosesView{false};
std::atomic<bool> g_escapeRequested{false};
// Set when overlay code faults: from then on the overlay stays off and every hook passes through.
std::atomic<bool> g_disabled{false};
// Published by the render thread after ImGui::NewFrame, read by the window procedure.
std::atomic<bool> g_wantMouse{false};
std::atomic<bool> g_wantKeyboard{false};

// Window messages arrive on the game thread and ImGui runs on the render thread, so they are queued.
std::mutex g_inputMutex;
std::vector<InputEvent> g_inputQueue;

bool OverlayHasCursor() {
    return g_visible && !g_disabled;
}

BOOL WINAPI HkSetCursorPos(int x, int y) {
    return OverlayHasCursor() ? TRUE : g_origSetCursorPos(x, y);
}

BOOL WINAPI HkClipCursor(const RECT* rect) {
    return g_origClipCursor(OverlayHasCursor() ? nullptr : rect);
}

bool IsRelease(UINT m) {
    return m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP || m == WM_XBUTTONUP || m == WM_KEYUP || m == WM_SYSKEYUP;
}

// Left, right and middle button down, up and double click (a double click is a press).
bool IsMouseButton(UINT m) {
    return m >= WM_LBUTTONDOWN && m <= WM_MBUTTONDBLCLK;
}

bool IsKey(UINT m) {
    return m >= WM_KEYFIRST && m <= WM_KEYLAST;
}

bool KeyDown(int vk) {
    return (GetKeyState(vk) & 0x8000) != 0;
}

void QueueInput(UINT m, WPARAM w, LPARAM l) {
    const InputEvent e{m, w, l, KeyDown(VK_CONTROL), KeyDown(VK_SHIFT), KeyDown(VK_MENU)};
    std::lock_guard lock(g_inputMutex);
    if (g_inputQueue.size() < kMaxQueuedInput) g_inputQueue.push_back(e);
}

LRESULT CALLBACK HkWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    const WNDPROC orig = g_origWndProc.load();
    if (!orig) return DefWindowProcW(h, m, w, l);
    if (OverlayHasCursor()) {
        try {
            // Esc hides a view the user opened; with a game panel open it goes to the game, which
            // closes the panel, and the overlay follows the close signal.
            if (m == WM_KEYDOWN && w == VK_ESCAPE && g_escapeClosesView && !g_wantKeyboard) {
                g_escapeRequested = true;
                return 0;
            }
            const bool wheel = m == WM_MOUSEWHEEL || m == WM_MOUSEHWHEEL;
            const bool key = IsKey(m);
            // Keyboard goes to ImGui only while typing in a field (releases always, so no key sticks);
            // ImGui's keyboard navigation is off, so keys meant for the game never press overlay buttons.
            const bool focus = m == WM_SETFOCUS || m == WM_KILLFOCUS;
            if (wheel || focus || IsMouseButton(m) || (key && (g_wantKeyboard || IsRelease(m)))) QueueInput(m, w, l);
            // Releases always reach the game, so nothing stays held there either.
            if (!IsRelease(m)) {
                const bool mouse = (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || m == WM_INPUT || m == WM_SETCURSOR;
                if (mouse && g_wantMouse) {
                    if (m == WM_INPUT) return DefWindowProcW(h, m, w, l);  // required for raw input cleanup
                    return m == WM_SETCURSOR ? TRUE : 0;
                }
                if (key && g_wantKeyboard) return 0;
            }
        } catch (...) {
            // Never let the overlay break the game's window.
            DisableOverlay("overlay disabled after a fault in the window procedure");
        }
    }
    return CallWindowProcW(orig, h, m, w, l);
}

// Replaces one import of the game executable (UE calls SetCursorPos/ClipCursor through its import
// table). The import is matched by the system function's address; an import another hook already
// changed is left alone.
bool PatchIat(HMODULE module, const char* dll, const char* function, void* replacement) {
    auto base = (BYTE*)module;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imports.VirtualAddress) return false;
    void* target = (void*)GetProcAddress(GetModuleHandleA(dll), function);
    if (!target) return false;
    for (auto desc = (IMAGE_IMPORT_DESCRIPTOR*)(base + imports.VirtualAddress); desc->Name; ++desc) {
        if (_stricmp((const char*)(base + desc->Name), dll) != 0) continue;
        for (auto thunk = (IMAGE_THUNK_DATA*)(base + desc->FirstThunk); thunk->u1.Function; ++thunk) {
            if ((void*)thunk->u1.Function != target) continue;
            DWORD old;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &old)) return false;
            InterlockedExchangePointer((PVOID*)&thunk->u1.Function, replacement);
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

// IDXGISwapChain's vtable is shared by every swap chain in the process, so patching the one of a
// throwaway swap chain also redirects the game's.
bool HookPresent(HMODULE self) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = self;
    wc.lpszClassName = L"ResearchOverlayDummy";
    RegisterClassExW(&wc);
    HWND window = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, self, nullptr);
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = window;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc,
                                               &device, nullptr, &context);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &device,
                                           nullptr, &context);
    }
    bool ok = false;
    if (SUCCEEDED(hr)) {
        void** vtable = *(void***)sc;
        DWORD old;
        if (VirtualProtect(&vtable[kPresentVtableSlot], sizeof(void*), PAGE_READWRITE, &old)) {
            g_origPresent = (PresentFn)vtable[kPresentVtableSlot];
            InterlockedExchangePointer(&vtable[kPresentVtableSlot], (void*)&HkPresent);
            VirtualProtect(&vtable[kPresentVtableSlot], sizeof(void*), old, &old);
            ok = true;
        }
    }
    if (sc) sc->Release();
    if (context) context->Release();
    if (device) device->Release();
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, self);
    return ok;
}

void LogHook(const char* name, bool ok) {
    Log(std::string("hook ") + name + ": " + (ok ? "ok" : "FAILED"));
}

}  // namespace

void InstallHooks(HMODULE self) {
    const HMODULE exe = GetModuleHandleW(nullptr);
    LogHook("SetCursorPos", PatchIat(exe, "USER32.dll", "SetCursorPos", (void*)&HkSetCursorPos));
    LogHook("ClipCursor", PatchIat(exe, "USER32.dll", "ClipCursor", (void*)&HkClipCursor));
    LogHook("Present", HookPresent(self));
}

HRESULT CallOriginalPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags) {
    return g_origPresent(swapChain, syncInterval, flags);
}

void SubclassGameWindow(HWND hwnd) {
    // Store the original window procedure before ours can run, then subclass.
    g_origWndProc = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    const WNDPROC previous = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)HkWndProc);
    if (previous) g_origWndProc = previous;
}

bool WindowProcReplaced(HWND hwnd) {
    return (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC) != HkWndProc;
}

void PublishView(bool visible, bool escapeClosesView) {
    if (g_disabled) return;
    g_escapeClosesView = escapeClosesView;
    const bool wasVisible = g_visible.exchange(visible);
    // The game may have confined the cursor; while the overlay is up, HkClipCursor keeps it free.
    if (visible && !wasVisible) g_origClipCursor(nullptr);
}

void PublishWants(bool mouse, bool keyboard) {
    g_wantMouse = mouse;
    g_wantKeyboard = keyboard;
}

bool TakeEscapeRequest() {
    return g_escapeRequested.exchange(false);
}

void DisableOverlay(const std::string& reason) {
    const bool wasDisabled = g_disabled.exchange(true);
    g_visible = false;
    if (!wasDisabled) Log(reason);
}

bool OverlayDisabled() {
    return g_disabled;
}

std::vector<InputEvent> TakeInputEvents() {
    std::vector<InputEvent> taken;
    std::lock_guard lock(g_inputMutex);
    taken.swap(g_inputQueue);
    return taken;
}

}  // namespace rd
