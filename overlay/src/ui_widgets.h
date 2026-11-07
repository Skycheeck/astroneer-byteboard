#pragma once
// Drawing pieces shared by the catalog and printer windows.
// Threads: render thread only, between ImGui::NewFrame and ImGui::Render.
#include <string>
#include <string_view>

#include "bridge.h"
#include "catalog.h"
#include "imgui.h"
#include "ui_state.h"
#include "ui_style.h"

namespace rd {

float TextWidth(ImFont* font, float size, std::string_view text);
// The text cut at the end with "…" so that it fits into maxWidth.
std::string FitText(ImFont* font, float size, const std::string& text, float maxWidth);

// A heading in display caps.
void Caps(const char* text, float size, uint32_t rgb);
void Wrapped(uint32_t rgb, const std::string& text);
// The filled blue button with white display caps.
bool PrimaryButton(const std::string& label, ImVec2 size);
void SearchBox(const char* id, const char* hint, char* buf, size_t size);
void KeyValue(const char* key, const std::string& value, uint32_t rgb = C::fg);

// Header strip shared by all windows: title on the left; close, bytes and (optionally) the
// research switch with the connection state on the right. True when the close button was clicked.
bool DrawHeader(const std::string& title, const char* subtitle, const Live& live, bool withAllowSwitch);

// Items placed left to right that wrap to a new line when the next one does not fit.
class Flow {
public:
    // indent < 0: a wrapped line starts at the window's own indent.
    Flow(float spacing, float rightMargin, float indent) : spacing_(spacing), rightMargin_(rightMargin), indent_(indent) {}
    // Moves the cursor to where the next item of this width goes.
    void Next(float width);

private:
    float spacing_;
    float rightMargin_;
    float indent_;
    bool first_ = true;
};

// One filter row: a caps label on the left and chips that wrap back to the row's indent.
class ChipRow {
public:
    explicit ChipRow(const char* label);
    bool Chip(const char* id, const std::string& text, int count, bool selected);

private:
    Flow flow_;
};

// "1 500 Б", "изучено", or for a reward "за задание" / "за событие" / "особое".
std::string CostText(const Item& item, Status status);
// How the item is unlocked, as a sentence: "Награда за задание «…»", "Исследование за 1 000 Б".
std::string UnlockText(const Item& item);

// Icon colour by status. In the catalog the active items are the ones that can be researched now;
// in a printer list they are the ones that can be printed (researched).
enum class IconContext { Catalog, Printer };
uint32_t IconTint(Status status, IconContext context);

// "2× Смола"
std::string IngredientLabel(const Ingredient& ingredient);

}  // namespace rd
