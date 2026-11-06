#pragma once
// Colours, fonts, layout constants and the item icon atlas.
// Threads: render thread only. Fonts and the atlas are created during InitImGui, on the first frame.
#include <d3d11.h>

#include <cstdint>

#include "imgui.h"

namespace rd {

// Palette, 0xRRGGBB, named by role.
namespace C {
constexpr uint32_t bg = 0xdfe5ea;
constexpr uint32_t panel = 0xffffff;
constexpr uint32_t panelAlt = 0xf1f4f6;  // researched cards, recipe chips, icon tiles
constexpr uint32_t line = 0xdde3e8;
constexpr uint32_t edge = 0xcfd7de;
constexpr uint32_t fg = 0x363c44;
constexpr uint32_t muted = 0x8a949d;
constexpr uint32_t faint = 0xb3bcc4;
constexpr uint32_t accent = 0x1b8fe2;
constexpr uint32_t accentDeep = 0x1478c4;
constexpr uint32_t accentSoft = 0xe2f0fb;
constexpr uint32_t iconActive = accent;  // the item can be acted on in this window
constexpr uint32_t iconMuted = 0xa9b2ba;
constexpr uint32_t iconFaint = 0xc9d0d6;
constexpr uint32_t warn = 0xd2554b;
constexpr uint32_t warnSoft = 0xfbe9e7;
constexpr uint32_t white = 0xffffff;
}  // namespace C

ImU32 Col(uint32_t rgb, float alpha = 1);
ImVec4 Vec(uint32_t rgb, float alpha = 1);

// Layout, in pixels.
constexpr float kHeaderH = 64;
constexpr float kPad = 20;
constexpr float kLabelW = 140;   // filter row labels
constexpr float kCardMinW = 230;
constexpr float kCardH = 70;
constexpr float kGap = 10;
constexpr float kRowMinW = 380;  // printer list rows
constexpr float kRowH = 66;

ImFont* BodyFont();     // Segoe UI
ImFont* BoldFont();     // Segoe UI Semibold
ImFont* DisplayFont();  // Bahnschrift, the closest system face to the game's squared caps

void LoadFonts(ImGuiIO& io);
void ApplyStyle();
// Item icons: white silhouettes in one RGBA atlas (<mod>/data/icons.rgba), tinted when drawn.
void LoadIconAtlas(ID3D11Device* device);
void DrawIcon(ImDrawList* dl, int index, ImVec2 pos, float size, uint32_t rgb);

}  // namespace rd
