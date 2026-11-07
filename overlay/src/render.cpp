#include "render.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <cfloat>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "bridge.h"
#include "hooks.h"
#include "lang.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "paths.h"
#include "ui_state.h"
#include "ui_style.h"
#include "ui_windows.h"
#include "util.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
// Defined (non-static, "to allow third-party code to use that") in imgui_impl_win32.cpp, not in its header.
ImGuiKey ImGui_ImplWin32_KeyEventToImGuiKey(WPARAM wParam, LPARAM lParam);

namespace rd {

namespace {

using Microsoft::WRL::ComPtr;

constexpr int kMouseButtons = 3;  // left, right, middle

// The device and context are kept for the life of the process and never released: the DLL is
// pinned, so the only time static destructors would run is process exit, when d3d11 may be gone.
ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
HWND g_hwnd = nullptr;
IDXGISwapChain* g_gameSwapChain = nullptr;  // compared by address only, re-checked when it changes
bool g_imguiReady = false;
bool g_initFailed = false;
bool g_reportedWndProc = false;
bool g_drewLastFrame = false;
bool g_wasForeground = false;
bool g_buttonDown[kMouseButtons] = {};
FrameHooks g_frameHooks;

// Only the game's own window ("UnrealWindow") gets the overlay; other swap chains pass through.
bool IsGameSwapChain(IDXGISwapChain* sc, HWND* window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc)) || !desc.OutputWindow) return false;
    wchar_t className[64]{};
    GetClassNameW(desc.OutputWindow, className, 64);
    *window = desc.OutputWindow;
    return wcscmp(className, L"UnrealWindow") == 0;
}

bool AcceptSwapChain(IDXGISwapChain* sc, HWND& window) {
    window = g_hwnd;
    if (sc == g_gameSwapChain) return true;
    if (!IsGameSwapChain(sc, &window) || (g_hwnd && window != g_hwnd)) return false;
    if (g_device) {
        // A new swap chain on another device (a renderer restart) is not drawn on.
        ComPtr<ID3D11Device> device;
        if (FAILED(sc->GetDevice(IID_PPV_ARGS(&device))) || device.Get() != g_device) return false;
    }
    g_gameSwapChain = sc;
    return true;
}

bool InitImGui(IDXGISwapChain* sc, HWND hwnd) {
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_device)))) return false;  // not D3D11
    g_device->GetImmediateContext(&g_context);
    g_hwnd = hwnd;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    static const std::string iniPath = Utf8(Paths().out / "overlay.ini");
    io.IniFilename = g_frameHooks.persistLayout ? iniPath.c_str() : nullptr;
    // No keyboard navigation: keys pressed for the game must never press overlay buttons.
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    // No mouse cursor changes: ImGui draws the cursor; SetCursor from the render thread does nothing.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    LoadFonts(io);
    ApplyStyle();
    LoadIconAtlas(g_device);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    // No IME calls: ImGui's default handler calls ImmGetContext/ImmSetCompositionWindow on the game
    // window from the render thread. Two game crashes had Windows text input (inputhost.dll) on the
    // faulting render-side thread right after the overlay changed focus. Plain typing (WM_CHAR,
    // Cyrillic included) does not need it.
    ImGui::GetPlatformIO().Platform_SetImeDataFn = nullptr;
    SubclassGameWindow(hwnd);
    return true;
}

void SetButton(ImGuiIO& io, int button, bool down) {
    io.AddMouseButtonEvent(button, down);
    g_buttonDown[button] = down;
}

// The ImGui button of a mouse button message, or -1.
int MouseButtonOf(UINT message, bool& down) {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            down = true;
            return 0;
        case WM_LBUTTONUP:
            down = false;
            return 0;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
            down = true;
            return 1;
        case WM_RBUTTONUP:
            down = false;
            return 1;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
            down = true;
            return 2;
        case WM_MBUTTONUP:
            down = false;
            return 2;
        default:
            return -1;
    }
}

void AddKeyEvent(ImGuiIO& io, const InputEvent& e) {
    if (e.wParam >= 256) return;
    const bool down = e.message == WM_KEYDOWN || e.message == WM_SYSKEYDOWN;
    const ImGuiKey key = ImGui_ImplWin32_KeyEventToImGuiKey(e.wParam, e.lParam);
    if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
}

// The physical state of ImGui's left, right and middle buttons; all up while the game window is
// not in the foreground. GetAsyncKeyState reads the physical buttons, so with swapped mouse buttons
// the logical left button (WM_LBUTTONDOWN) is VK_RBUTTON.
void ReadPhysicalButtons(bool foreground, bool (&down)[kMouseButtons]) {
    const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    const int vk[kMouseButtons] = {swapped ? VK_RBUTTON : VK_LBUTTON, swapped ? VK_LBUTTON : VK_RBUTTON, VK_MBUTTON};
    for (int b = 0; b < kMouseButtons; ++b) down[b] = foreground && (GetAsyncKeyState(vk[b]) & 0x8000) != 0;
}

// Feeds ImGui on the render thread. The mouse position comes from the OS every frame. Buttons,
// wheel and keys come from the window procedure's queue, in order and each after the modifier
// state recorded with it, so a click that starts and ends between two frames still counts (ImGui
// spreads the press and the release over two frames) and Ctrl/Shift shortcuts work in text fields.
// ImGui's Win32 handler is not used for these: it calls TrackMouseEvent and SetCapture, which only
// work on the window's own (game) thread, and reads modifiers with GetKeyState, which is per-thread.
// It still handles characters and focus changes.
// The physical button state (GetAsyncKeyState) fills two gaps of the queue. It resyncs the buttons
// when the overlay appears or the game window gains or loses focus. And it releases a held button
// whose release never came: nothing captures the mouse, so a button released outside the game
// window (a drag in windowed mode) sends its WM_*BUTTONUP to another window.
void FeedInput(ImGuiIO& io, const std::vector<InputEvent>& events, bool resyncButtons) {
    const bool foreground = GetForegroundWindow() == g_hwnd;
    POINT pt{};
    if (foreground && GetCursorPos(&pt) && ScreenToClient(g_hwnd, &pt)) io.AddMousePosEvent((float)pt.x, (float)pt.y);
    else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    bool queuedButton[kMouseButtons] = {};
    for (const InputEvent& e : events) {
        io.AddKeyEvent(ImGuiMod_Ctrl, e.ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, e.shift);
        io.AddKeyEvent(ImGuiMod_Alt, e.alt);
        bool down = false;
        const int button = MouseButtonOf(e.message, down);
        if (button >= 0) {
            SetButton(io, button, down);
            queuedButton[button] = true;
            continue;
        }
        switch (e.message) {
            case WM_MOUSEWHEEL:
                io.AddMouseWheelEvent(0, (float)GET_WHEEL_DELTA_WPARAM(e.wParam) / WHEEL_DELTA);
                break;
            case WM_MOUSEHWHEEL:
                io.AddMouseWheelEvent(-(float)GET_WHEEL_DELTA_WPARAM(e.wParam) / WHEEL_DELTA, 0);
                break;
            case WM_KEYDOWN:
            case WM_KEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSKEYUP:
                AddKeyEvent(io, e);
                break;
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
                resyncButtons = true;
                ImGui_ImplWin32_WndProcHandler(g_hwnd, e.message, e.wParam, e.lParam);
                break;
            default:  // WM_CHAR and the other character messages
                ImGui_ImplWin32_WndProcHandler(g_hwnd, e.message, e.wParam, e.lParam);
                break;
        }
    }
    if (foreground != g_wasForeground) resyncButtons = true;
    g_wasForeground = foreground;
    bool physical[kMouseButtons];
    ReadPhysicalButtons(foreground, physical);
    for (int b = 0; b < kMouseButtons; ++b) {
        if (resyncButtons) {
            if (physical[b] != g_buttonDown[b]) SetButton(io, b, physical[b]);
            continue;
        }
        // Only lets go of a button held since an earlier frame, never presses one: presses come
        // from the queue alone, and the queue stays authoritative for a button it reported this frame.
        if (g_buttonDown[b] && !queuedButton[b] && !physical[b]) SetButton(io, b, false);
    }
}

// A render target view of this frame's back buffer, released at the end of the frame, so the
// overlay never holds the game's back buffer between frames (a swap-chain resize needs it free).
ComPtr<ID3D11RenderTargetView> CreateFrameRtv(IDXGISwapChain* sc) {
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return nullptr;
    D3D11_TEXTURE2D_DESC td{};
    back->GetDesc(&td);
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    // Typeless back buffers need an explicit UNORM view.
    switch (td.Format) {
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
            break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            break;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            break;
        default:
            desc.Format = td.Format;
            break;
    }
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv;
    g_device->CreateRenderTargetView(back.Get(), &desc, rtv.GetAddressOf());
    return rtv;
}

// Binds the overlay's render target and restores the game's, also when a fault unwinds the frame
// (the DLL is built with /EHa, so destructors run on structured exceptions).
class RenderTargetScope {
public:
    RenderTargetScope(ID3D11DeviceContext* context, ID3D11RenderTargetView* rtv) : context_(context) {
        context_->OMGetRenderTargets(1, previousRtv_.GetAddressOf(), previousDsv_.GetAddressOf());
        context_->OMSetRenderTargets(1, &rtv, nullptr);
    }
    ~RenderTargetScope() { context_->OMSetRenderTargets(1, previousRtv_.GetAddressOf(), previousDsv_.Get()); }
    RenderTargetScope(const RenderTargetScope&) = delete;
    RenderTargetScope& operator=(const RenderTargetScope&) = delete;

private:
    ID3D11DeviceContext* context_;
    ComPtr<ID3D11RenderTargetView> previousRtv_;
    ComPtr<ID3D11DepthStencilView> previousDsv_;
};

void PublishViewState(const ViewState& view) {
    PublishView(view.Visible(), view.OwnsEscape());
}

// Everything the overlay does in one Present: init, view changes, input, drawing.
void OverlayFrame(IDXGISwapChain* sc) {
    HWND window = nullptr;
    if (!AcceptSwapChain(sc, window)) return;
    if (!g_imguiReady && !g_initFailed) {
        g_imguiReady = InitImGui(sc, window);
        g_initFailed = !g_imguiReady;
    }
    if (!g_imguiReady) return;

    // Another hook replacing the window procedure is only logged: re-installing ours on top could
    // make the two call each other forever.
    if (!g_reportedWndProc && WindowProcReplaced(g_hwnd)) {
        g_reportedWndProc = true;
        Log("note: the game window procedure was replaced by another hook; clicks and keys may stop reaching the overlay");
    }

    UiState& ui = Ui();
    if (TakeEscapeRequest()) ui.view.OnEscape();
    ApplyRequests(ui, TakeRequests());
    PublishViewState(ui.view);

    const std::vector<InputEvent> input = TakeInputEvents();
    if (!ui.view.Visible()) {
        PublishWants(false, false);
        g_drewLastFrame = false;
        return;
    }
    // Owned from creation, so a fault anywhere in the frame still releases it: a view left alive
    // holds the game's back buffer, and the game's next ResizeBuffers would fail.
    const ComPtr<ID3D11RenderTargetView> rtv = CreateFrameRtv(sc);
    if (!rtv) {
        g_drewLastFrame = false;  // input of this frame is lost: resync the buttons next time
        return;  // no render target this frame (e.g. mid-resize): skip drawing
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    FeedInput(io, input, !g_drewLastFrame);
    io.MouseDrawCursor = true;
    ImGui::NewFrame();
    g_drewLastFrame = true;
    if (ui.dropFocus) {
        ImGui::SetWindowFocus(nullptr);
        io.ClearInputKeys();
        ui.dropFocus = false;
    }
    PublishWants(io.WantCaptureMouse, io.WantTextInput);
    const std::shared_ptr<const Live> live = SnapshotLive();
    SetFrameLanguage(live->language, DataLocales());
    ResolvePending(ui, *live);
    if (ui.view.GetMode() == Mode::Printer) DrawPrinterWindow(ui, *live);
    else DrawCatalogWindow(ui, *live);
    PublishViewState(ui.view);  // the close button may have hidden the view
    ImGui::Render();
    {
        RenderTargetScope scope(g_context, rtv.Get());
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
    if (g_frameHooks.afterFrame) g_frameHooks.afterFrame(sc);
}

int CaptureFaultAddress(EXCEPTION_POINTERS* info, void** address) {
    *address = info->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;
}

// Structured-exception guard (no C++ objects in this function, as __try requires). With /EHa the
// destructors inside OverlayFrame still run while a fault unwinds it.
DWORD RunFrameGuarded(IDXGISwapChain* sc, void** faultAddress) {
    __try {
        OverlayFrame(sc);
        return 0;
    } __except (CaptureFaultAddress(GetExceptionInformation(), faultAddress)) {
        return GetExceptionCode();
    }
}

// "Byteboard.dll+0x1a2b3", or the bare address outside any module.
std::string DescribeAddress(void* address) {
    char text[MAX_PATH + 32];
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH];
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (address && GetModuleHandleExW(flags, (LPCWSTR)address, &module) && GetModuleFileNameW(module, path, MAX_PATH)) {
        const std::string name = Utf8(fs::path(path).filename());
        snprintf(text, sizeof text, "%s+0x%llx", name.c_str(), (unsigned long long)((uintptr_t)address - (uintptr_t)module));
    } else {
        snprintf(text, sizeof text, "%p", address);
    }
    return text;
}

}  // namespace

HRESULT STDMETHODCALLTYPE HkPresent(IDXGISwapChain* sc, UINT syncInterval, UINT flags) {
    if (!OverlayDisabled()) {
        void* faultAddress = nullptr;
        if (const DWORD code = RunFrameGuarded(sc, &faultAddress)) {
            char hex[16];
            snprintf(hex, sizeof hex, "0x%08lX", code);
            DisableOverlay(std::string("overlay disabled after exception ") + hex + " at " + DescribeAddress(faultAddress) + " in a frame");
        }
    }
    return CallOriginalPresent(sc, syncInterval, flags);
}

void SetFrameHooks(const FrameHooks& hooks) {
    g_frameHooks = hooks;
}

}  // namespace rd
