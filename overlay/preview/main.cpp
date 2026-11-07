// Renders the overlay outside the game. It creates a D3D11 window with Unreal's class name
// ("UnrealWindow"), loads ByteboardPreview.dll (the overlay plus src/preview_capture.cpp)
// from its own folder, opens a view through Byteboard_Preview and presents frames until the
// DLL has saved one as a BMP.
//   preview.exe <out.bmp> [itemId | printer:ItemCategory_...] [width] [height]
//   preview.exe --unload-test
// Exit codes: 0 done; 1 bad arguments or setup; 2 the DLL was unloaded (unload test); 3 the overlay
// did not hook Present or did not capture a frame (mod/Byteboard/out/overlay.log says why).
#include <windows.h>

#include <d3d11.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

constexpr int kPresentVtableSlot = 8;  // IDXGISwapChain::Present, the slot the overlay patches
constexpr ULONGLONG kHookTimeoutMs = 10000;
constexpr int kMaxFrames = 300;
constexpr UINT kMinSide = 64;
constexpr UINT kMaxSide = 8192;

std::string ToUtf8(const wchar_t* text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string utf8((size_t)n, '\0');  // n counts the terminating zero
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), n, nullptr, nullptr);
    utf8.resize((size_t)n - 1);
    return utf8;
}

void PumpMessages() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
}

void* PresentSlot(IDXGISwapChain* sc) {
    return (*(void***)sc)[kPresentVtableSlot];
}

// The overlay hooks Present on its own thread, after it has read the catalog.
bool WaitForPresentHook(IDXGISwapChain* sc, void* original) {
    const ULONGLONG start = GetTickCount64();
    while (PresentSlot(sc) == original) {
        if (GetTickCount64() - start > kHookTimeoutMs) return false;
        PumpMessages();
        Sleep(20);
    }
    return true;
}

bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring out = argc > 1 ? argv[1] : L"preview.bmp";
    const bool unloadTest = out == L"--unload-test";
    const std::string selection = argc > 2 ? ToUtf8(argv[2]) : std::string();
    const UINT w = argc > 3 ? (UINT)_wtoi(argv[3]) : 1600;
    const UINT h = argc > 4 ? (UINT)_wtoi(argv[4]) : 900;
    if (w < kMinSide || w > kMaxSide || h < kMinSide || h > kMaxSide) {
        fprintf(stderr, "bad size\n");
        return 1;
    }

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"UnrealWindow";
    RegisterClassExW(&wc);
    RECT r{0, 0, (LONG)w, (LONG)h};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"preview", WS_OVERLAPPEDWINDOW, 40, 40, r.right - r.left, r.bottom - r.top, nullptr,
                              nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        fprintf(stderr, "cannot create window\n");
        return 1;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = w;
    sd.BufferDesc.Height = h;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                             nullptr, &ctx))) {
        fprintf(stderr, "D3D11 init failed\n");
        return 1;
    }
    void* originalPresent = PresentSlot(sc);

    wchar_t dll[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, dll, MAX_PATH);
    wchar_t* slash = n && n < MAX_PATH ? wcsrchr(dll, L'\\') : nullptr;
    if (!slash) {
        fprintf(stderr, "bad module path\n");
        return 1;
    }
    // preview.exe sits in overlay/bin and the preview DLL reads <repo>/mod/Byteboard/data. Without
    // catalog.tsv there the overlay stays off, so the reason is printed here.
    const std::wstring binFolder(dll, slash + 1);
    if (!FileExists(binFolder + L"..\\..\\mod\\Byteboard\\data\\catalog.tsv")) {
        fprintf(stderr, "mod\\Byteboard\\data\\catalog.tsv is missing: run \"overlay\\bin\\byteboard-extract.exe <pak> "
                        "mod\\Byteboard\\data\" in the repository first\n");
        return 1;
    }
    if (wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - dll), L"ByteboardPreview.dll") != 0) {
        fprintf(stderr, "bad module path\n");
        return 1;
    }
    HMODULE mod = LoadLibraryW(dll);
    if (!mod) {
        fprintf(stderr, "cannot load ByteboardPreview.dll\n");
        return 1;
    }
    if (!WaitForPresentHook(sc, originalPresent)) {
        fprintf(stderr, "the overlay did not hook Present (see mod/Byteboard/out/overlay.log)\n");
        return 3;
    }

    if (unloadTest) {
        // What a UE4SS hot reload does: the Lua state frees the library. A pinned DLL must survive.
        FreeLibrary(mod);
        FreeLibrary(mod);
        const bool alive = GetModuleHandleW(L"ByteboardPreview.dll") != nullptr;
        printf("after FreeLibrary x2: %s\n", alive ? "still loaded (pinned)" : "UNLOADED");
        return alive ? 0 : 2;
    }

    const auto preview = (void (*)(const char*, const wchar_t*))GetProcAddress(mod, "Byteboard_Preview");
    if (!preview) {
        fprintf(stderr, "Byteboard_Preview not exported\n");
        return 1;
    }
    DeleteFileW(out.c_str());  // a file left from an earlier run must not count as this capture
    preview(selection.c_str(), out.c_str());

    const float sky[4] = {0.47f, 0.74f, 0.86f, 1};  // stand-in for the game behind the overlay
    bool saved = false;
    for (int frame = 0; frame < kMaxFrames && !saved; ++frame) {
        PumpMessages();
        ID3D11Texture2D* back = nullptr;
        ID3D11RenderTargetView* rtv = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) && SUCCEEDED(dev->CreateRenderTargetView(back, nullptr, &rtv))) {
            ctx->ClearRenderTargetView(rtv, sky);
        }
        if (rtv) rtv->Release();
        if (back) back->Release();
        sc->Present(1, 0);
        saved = FileExists(out);
    }
    if (!saved) {
        fprintf(stderr, "no frame was captured (see mod/Byteboard/out/overlay.log)\n");
        return 3;
    }
    printf("saved %ls\n", out.c_str());
    return 0;
}
