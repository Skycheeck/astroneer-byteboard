#include "ui_widgets.h"

#include <cfloat>

#include "util.h"

namespace rd {

namespace {

constexpr float kHeaderTitleY = 13;
constexpr float kHeaderTitleSize = 30;
constexpr float kSubtitleMinWindowW = 1200;  // narrower windows show only the title
constexpr const char* kEllipsis = "…";

}  // namespace

float TextWidth(ImFont* font, float size, std::string_view text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0, text.data(), text.data() + text.size()).x;
}

std::string FitText(ImFont* font, float size, const std::string& text, float maxWidth) {
    if (TextWidth(font, size, text) <= maxWidth) return text;
    std::string cut = text;
    while (!cut.empty()) {
        // Drop one UTF-8 character: its continuation bytes and its lead byte.
        size_t end = cut.size() - 1;
        while (end > 0 && ((unsigned char)cut[end] & 0xC0) == 0x80) --end;
        cut.resize(end);
        while (!cut.empty() && cut.back() == ' ') cut.pop_back();
        const std::string candidate = cut + kEllipsis;
        if (TextWidth(font, size, candidate) <= maxWidth) return candidate;
    }
    return {};
}

void Caps(const char* text, float size, uint32_t rgb) {
    ImGui::PushFont(DisplayFont(), size);
    ImGui::TextColored(Vec(rgb), "%s", Upper(text).c_str());
    ImGui::PopFont();
}

void Wrapped(uint32_t rgb, const std::string& text) {
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored(Vec(rgb), "%s", text.c_str());
    ImGui::PopTextWrapPos();
}

bool PrimaryButton(const std::string& label, ImVec2 size) {
    ImGui::PushFont(DisplayFont(), 18);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec(C::white));
    const bool clicked = ImGui::Button(label.c_str(), size);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    return clicked;
}

void SearchBox(const char* id, const char* hint, char* buf, size_t size) {
    ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 2 * kPad);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {14, 10});
    ImGui::InputTextWithHint(id, hint, buf, size);
    ImGui::PopStyleVar();
}

void KeyValue(const char* key, const std::string& value, uint32_t rgb) {
    const float x = ImGui::GetCursorPosX();
    ImGui::PushFont(DisplayFont(), 14);
    ImGui::TextColored(Vec(C::muted), "%s", Upper(key).c_str());
    ImGui::PopFont();
    ImGui::SameLine(x + 90);
    Wrapped(rgb, value);
}

bool DrawHeader(const std::string& title, const char* subtitle, const Live& live, bool withAllowSwitch) {
    ImDrawList* wdl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float ww = ImGui::GetWindowWidth();
    wdl->AddRectFilled(wp, wp + ImVec2(ww, kHeaderH), Col(C::panel), 4, ImDrawFlags_RoundCornersTop);
    wdl->AddRectFilled(wp + ImVec2(0, kHeaderH - 3), wp + ImVec2(ww, kHeaderH), Col(C::accent));
    ImGui::SetCursorPos({kPad, kHeaderTitleY});
    ImGui::PushFont(DisplayFont(), kHeaderTitleSize);
    ImGui::TextColored(Vec(C::accent), "%s", Upper(title).c_str());
    ImGui::PopFont();
    if (subtitle && ww >= kSubtitleMinWindowW) {
        ImGui::SameLine(0, 14);
        ImGui::SetCursorPosY(27);
        Caps(subtitle, 14, C::muted);
    }

    // Right to left: close, bytes, then the research switch and the connection state.
    float x = ww - kPad;
    ImGui::SetCursorPos({x - 30, 14});
    const bool closeClicked = ImGui::InvisibleButton("close", {30, 34});
    wdl->AddText(BodyFont(), 30, wp + ImVec2(x - 26, 12), Col(ImGui::IsItemHovered() ? C::accent : C::muted), "×");
    x -= 30 + 22;
    const std::string bytes = FormatInt((long long)live.balance);
    const float bytesW = TextWidth(DisplayFont(), kHeaderTitleSize, bytes);
    wdl->AddText(DisplayFont(), kHeaderTitleSize, wp + ImVec2(x - bytesW, 14), Col(C::accent), bytes.c_str());
    x -= bytesW + 10;
    const float labelW = TextWidth(DisplayFont(), 13, "БАЙТЫ");
    wdl->AddText(DisplayFont(), 13, wp + ImVec2(x - labelW, 28), Col(C::muted), "БАЙТЫ");
    if (!withAllowSwitch) return closeClicked;

    x -= labelW + 26;
    wdl->AddLine(wp + ImVec2(x, 16), wp + ImVec2(x, kHeaderH - 16), Col(C::line));
    x -= 18;
    const char* connection = live.connected ? "игра подключена" : "нет связи с модом";
    const float connectionW = TextWidth(BodyFont(), 14, connection);
    wdl->AddText(BodyFont(), 14, wp + ImVec2(x - connectionW, 34), Col(live.connected ? C::muted : C::warn), connection);
    bool allow = live.allowUnlock;
    const float checkboxW = ImGui::GetFrameHeight() + 8 + TextWidth(BodyFont(), 16, "Изучение разрешено");
    ImGui::SetCursorPos({x - checkboxW, 6});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});
    if (ImGui::Checkbox("Изучение разрешено", &allow)) SetAllowUnlock(allow);
    ImGui::PopStyleVar();
    return closeClicked;
}

void Flow::Next(float width) {
    if (!first_) {
        ImGui::SameLine(0, spacing_);
        if (ImGui::GetContentRegionAvail().x < width + rightMargin_) {
            ImGui::NewLine();
            if (indent_ >= 0) ImGui::SetCursorPosX(indent_);
        }
    }
    first_ = false;
}

ChipRow::ChipRow(const char* label) : flow_(4, kPad, kPad + kLabelW) {
    const float y = ImGui::GetCursorPosY();
    ImGui::SetCursorPos({kPad, y + 6});
    Caps(label, 14, C::accent);
    ImGui::SetCursorPos({kPad + kLabelW, y});
}

bool ChipRow::Chip(const char* id, const std::string& text, int count, bool selected) {
    ImGui::PushFont(DisplayFont(), 15);
    const std::string caps = Upper(text);
    const std::string number = std::to_string(count);
    const ImVec2 textSize = ImGui::CalcTextSize(caps.c_str());
    const ImVec2 numberSize = ImGui::CalcTextSize(number.c_str());
    const ImVec2 size{10 + textSize.x + 6 + numberSize.x + 10, textSize.y + 10};
    flow_.Next(size.x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(p, p + size, Col(C::accent), 3);
    else if (ImGui::IsItemHovered()) dl->AddRectFilled(p, p + size, Col(C::accentSoft), 3);
    dl->AddText({p.x + 10, p.y + 5}, selected ? Col(C::white) : Col(count ? C::muted : C::faint), caps.c_str());
    dl->AddText({p.x + 16 + textSize.x, p.y + 5}, selected ? Col(C::white, 0.75f) : Col(C::faint), number.c_str());
    ImGui::PopFont();
    return clicked;
}

std::string CostText(const Item& item, Status status) {
    return status == Status::Researched ? "изучено" : FormatInt(item.cost) + " Б";
}

uint32_t IconTint(Status status, IconContext context) {
    if (context == IconContext::Printer) {
        if (status == Status::Researched) return C::iconActive;
        return status == Status::Affordable ? C::iconMuted : C::iconFaint;
    }
    if (status == Status::Affordable) return C::iconActive;
    return status == Status::TooExpensive ? C::iconMuted : C::iconFaint;
}

std::string IngredientLabel(const Ingredient& ingredient) {
    return std::to_string(ingredient.count) + "× " + ingredient.name;
}

}  // namespace rd
