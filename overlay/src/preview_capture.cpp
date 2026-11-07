// Preview support, linked only into ByteboardPreview.dll (see CMakeLists.txt): preview.exe
// opens the overlay on a chosen view without a game signal, and the DLL saves one
// frame as a BMP. The game DLL has no exports.
// Threads: preview.exe calls Byteboard_Preview from the thread that presents its frames, so
// everything here runs on the overlay's render thread.
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "bridge.h"
#include "render.h"
#include "ui_state.h"

namespace rd {

namespace {

using Microsoft::WRL::ComPtr;

// Fonts and layout settle over the first frames.
constexpr int kFramesBeforeCapture = 10;
constexpr const char* kPrinterPrefix = "printer:";

std::wstring g_capturePath;
int g_framesDrawn = 0;

// Writes the back buffer as a top-down 32-bit BMP.
void SaveBackBuffer(IDXGISwapChain* sc, const std::wstring& path) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&device))) || FAILED(sc->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    device->GetImmediateContext(context.GetAddressOf());
    D3D11_TEXTURE2D_DESC td{};
    back->GetDesc(&td);
    const bool rgba = td.Format == DXGI_FORMAT_R8G8B8A8_UNORM || td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&td, nullptr, staging.GetAddressOf()))) return;
    context->CopyResource(staging.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
    const DWORD headerBytes = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    BITMAPFILEHEADER fileHeader{0x4D42, (DWORD)(headerBytes + td.Width * td.Height * 4), 0, 0, headerBytes};
    BITMAPINFOHEADER infoHeader{sizeof infoHeader, (LONG)td.Width, -(LONG)td.Height, 1, 32, BI_RGB};
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)&fileHeader, sizeof fileHeader);
    f.write((const char*)&infoHeader, sizeof infoHeader);
    std::vector<uint8_t> row(td.Width * 4);
    for (UINT y = 0; y < td.Height; ++y) {
        memcpy(row.data(), (const uint8_t*)mapped.pData + y * mapped.RowPitch, row.size());
        if (rgba) {
            for (size_t x = 0; x < row.size(); x += 4) std::swap(row[x], row[x + 2]);  // BMP is BGRA
        }
        f.write((const char*)row.data(), row.size());
    }
    context->Unmap(staging.Get(), 0);
}

// Frames count only once the poll thread has read the mod's files, so the capture shows them.
void CaptureAfterFrames(IDXGISwapChain* sc) {
    if (g_capturePath.empty() || !SnapshotLive()->polled) return;
    if (++g_framesDrawn < kFramesBeforeCapture) return;
    SaveBackBuffer(sc, g_capturePath);
    g_capturePath.clear();
}

}  // namespace

}  // namespace rd

// Opens the overlay and saves a frame (after a few frames) as a BMP. selectId is an item id to
// select in the catalog, or "printer:<ItemCategory_...>" for that printer's list.
extern "C" __declspec(dllexport) void Byteboard_Preview(const char* selectId, const wchar_t* bmpPath) {
    rd::FrameHooks hooks;
    hooks.persistLayout = false;
    hooks.afterFrame = &rd::CaptureAfterFrames;
    rd::SetFrameHooks(hooks);
    rd::UiState& ui = rd::Ui();
    const std::string selection = selectId ? selectId : "";
    if (selection.rfind(rd::kPrinterPrefix, 0) == 0) {
        ui.view.OnSignal({rd::RequestKind::PrinterOpen, selection.substr(std::strlen(rd::kPrinterPrefix))});
    } else {
        ui.view.OpenUserCatalog();
        ui.selectedId = selection;
    }
    rd::g_capturePath = bmpPath ? bmpPath : L"";
    rd::g_framesDrawn = 0;
}
