#include "ui_style.h"

#include <windows.h>

#include <cstring>
#include <string>

#include "paths.h"
#include "util.h"

namespace rd {

namespace {

constexpr int kIconPx = 64;
constexpr int kIconsPerRow = 16;
constexpr uint32_t kMaxAtlasSide = 8192;
constexpr size_t kAtlasHeaderBytes = 8;  // u32 width, u32 height

ImFont* g_body = nullptr;
ImFont* g_bold = nullptr;
ImFont* g_display = nullptr;

// The atlas lives as long as the process (see the note on the device in render.cpp).
ID3D11ShaderResourceView* g_iconView = nullptr;
int g_atlasW = 0;
int g_atlasH = 0;

}  // namespace

ImU32 Col(uint32_t rgb, float a) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, (int)(a * 255));
}

ImVec4 Vec(uint32_t rgb, float a) {
    return ImGui::ColorConvertU32ToFloat4(Col(rgb, a));
}

ImFont* BodyFont() {
    return g_body;
}

ImFont* BoldFont() {
    return g_bold;
}

ImFont* DisplayFont() {
    return g_display;
}

void LoadFonts(ImGuiIO& io) {
    wchar_t windows[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    const fs::path fontDir = fs::path(windows) / "Fonts";
    const auto load = [&](const char* file) -> ImFont* {
        const fs::path f = fontDir / file;
        return FileExists(f) ? io.Fonts->AddFontFromFileTTF(Utf8(f).c_str(), 16.0f) : nullptr;
    };
    g_body = load("segoeui.ttf");
    if (!g_body) g_body = io.Fonts->AddFontDefault();
    g_bold = load("seguisb.ttf");
    if (!g_bold) g_bold = g_body;
    g_display = load("bahnschrift.ttf");
    if (!g_display) g_display = g_bold;
    io.FontDefault = g_body;
}

void ApplyStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4;
    s.ChildRounding = 3;
    s.FrameRounding = 3;
    s.PopupRounding = 3;
    s.GrabRounding = 3;
    s.ScrollbarRounding = 3;
    s.WindowPadding = {0, 0};
    s.FramePadding = {12, 9};
    s.ItemSpacing = {8, 8};
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 1;
    s.ScrollbarSize = 10;
    s.FontSizeBase = 17.0f;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = Vec(C::bg, 0.97f);
    c[ImGuiCol_ChildBg] = Vec(C::panel);
    c[ImGuiCol_PopupBg] = Vec(C::panel);
    c[ImGuiCol_Text] = Vec(C::fg);
    c[ImGuiCol_TextDisabled] = Vec(C::muted);
    c[ImGuiCol_Border] = Vec(C::edge);
    c[ImGuiCol_FrameBg] = Vec(C::panel);
    c[ImGuiCol_FrameBgHovered] = Vec(C::panel);
    c[ImGuiCol_FrameBgActive] = Vec(C::panel);
    c[ImGuiCol_Button] = Vec(C::accent);
    c[ImGuiCol_ButtonHovered] = Vec(C::accentDeep);
    c[ImGuiCol_ButtonActive] = Vec(C::accentDeep);
    c[ImGuiCol_CheckMark] = Vec(C::accent);
    c[ImGuiCol_Header] = Vec(C::accentSoft);
    c[ImGuiCol_HeaderHovered] = Vec(C::accentSoft);
    c[ImGuiCol_HeaderActive] = Vec(C::accentSoft);
    c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab] = Vec(C::faint);
    c[ImGuiCol_ScrollbarGrabHovered] = Vec(C::muted);
    c[ImGuiCol_ScrollbarGrabActive] = Vec(C::muted);
    c[ImGuiCol_NavCursor] = Vec(C::accent);
    c[ImGuiCol_TextSelectedBg] = Vec(C::accent, 0.25f);
    c[ImGuiCol_Separator] = Vec(C::line);
}

void LoadIconAtlas(ID3D11Device* device) {
    std::string atlas;
    if (!ReadSharedFile(Paths().overlay / "icons.rgba", atlas) || atlas.size() <= kAtlasHeaderBytes) return;
    uint32_t width = 0;
    uint32_t height = 0;
    memcpy(&width, atlas.data(), 4);
    memcpy(&height, atlas.data() + 4, 4);
    if (width == 0 || height == 0 || width > kMaxAtlasSide || height > kMaxAtlasSide) return;
    if (atlas.size() < kAtlasHeaderBytes + (size_t)width * height * 4) return;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{atlas.data() + kAtlasHeaderBytes, width * 4, 0};
    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(device->CreateTexture2D(&td, &init, &texture)) && SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view))) {
        g_iconView = view;
        g_atlasW = (int)width;
        g_atlasH = (int)height;
    }
    if (texture) texture->Release();  // the view keeps the texture alive
}

void DrawIcon(ImDrawList* dl, int index, ImVec2 p, float size, uint32_t rgb) {
    if (index < 0 || !g_iconView || g_atlasW < kIconPx || g_atlasH < kIconPx) return;
    if ((index / kIconsPerRow + 1) * kIconPx > g_atlasH) return;  // index outside the loaded atlas
    const float cx = (float)(index % kIconsPerRow);
    const float cy = (float)(index / kIconsPerRow);
    const ImVec2 uv0{cx * kIconPx / g_atlasW, cy * kIconPx / g_atlasH};
    const ImVec2 uv1{(cx + 1) * kIconPx / g_atlasW, (cy + 1) * kIconPx / g_atlasH};
    dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)g_iconView), p, p + ImVec2(size, size), uv0, uv1, Col(rgb));
}

}  // namespace rd
