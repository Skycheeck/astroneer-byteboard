// The printer window: the recipe on the printer with its ingredients and the print button (the
// overlay covers the printer's own button), then everything this printer can make.
#include <algorithm>
#include <cfloat>
#include <cmath>

#include "ui_widgets.h"
#include "lang.h"
#include "ui_windows.h"
#include "util.h"

namespace rd {

namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kBackpackCategory = "ItemCategory_Backpack";
constexpr ImVec2 kPrinterMinSize{420, 400};
constexpr ImGuiWindowFlags kWindowFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
constexpr float kArrowW = 40;     // the "›" strip of a row that opens the catalog
constexpr float kStripH = 64;     // the print strip and its button
constexpr float kStripNameX = 56;
constexpr float kStripMinNameW = 96;  // a status message never squeezes the recipe name below this
constexpr float kStripGap = 12;

// One item of the printer list. A researched item is selected on the printer by a click; a locked
// one opens in the research catalog, where its description is and where it can be researched.
// The "›" strip on the right always opens the catalog.
void PrinterRow(UiState& ui, const Item& it, Status status, float w, const Live& live) {
    ImGui::PushID(it.id.c_str());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size{w, kRowH};
    const bool clicked = ImGui::InvisibleButton("row", size - ImVec2(kArrowW, 0));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::SameLine(0, 0);
    const bool toCatalog = ImGui::InvisibleButton("cat", {kArrowW, kRowH});
    const bool arrowHovered = ImGui::IsItemHovered();
    if (arrowHovered) ImGui::SetTooltip("%s", T("Открыть в каталоге", "Open in the catalog"));
    const bool researched = status == Status::Researched;
    const bool current = researched && IsCurrentRecipe(it, live);
    dl->AddRectFilled(p, p + size, Col(current ? C::accentSoft : researched ? C::panel : C::panelAlt), 3);
    if (current) dl->AddRect(p + ImVec2(1, 1), p + size - ImVec2(1, 1), Col(C::accent), 3.0f, ImDrawFlags_None, 2.0f);
    else dl->AddRect(p, p + size, Col(hovered || arrowHovered ? C::accent : C::line), 3.0f);
    const float tile = 46;
    const ImVec2 tp = p + ImVec2(10, (kRowH - tile) / 2);
    dl->AddRectFilled(tp, tp + ImVec2(tile, tile), Col(researched ? C::panelAlt : C::panel), 3);
    DrawIcon(dl, it.icon, tp + ImVec2(7, 7), 32, IconTint(status, IconContext::Printer));

    float right = p.x + w - 12;
    const char* arrow = "›";
    const float arrowW = TextWidth(BodyFont(), 26, arrow);
    dl->AddText(BodyFont(), 26, {right - arrowW, p.y + kRowH / 2 - 17}, Col(arrowHovered ? C::accent : C::faint), arrow);
    right -= arrowW + 12;
    if (!researched) {
        const std::string cost = CostText(it, status);
        const float costW = TextWidth(BoldFont(), 15, cost);
        dl->AddText(BoldFont(), 15, {right - costW, p.y + kRowH / 2 - 9}, Col(status == Status::Affordable ? C::accent : C::muted), cost.c_str());
        right -= costW + 12;
    }

    const float textX = tp.x + tile + 12;
    const float textW = std::max(40.0f, right - textX);
    dl->PushClipRect(p, {right, p.y + kRowH}, true);
    dl->AddText(BoldFont(), 16, {textX, p.y + 11}, Col(researched ? C::fg : C::muted), NameOf(it).c_str());
    // The second line: what is happening with this row, otherwise its recipe.
    const std::string& recipe = RecipeOf(it);
    std::string sub = recipe.empty() ? SubgroupOf(it) : recipe;
    uint32_t subColor = C::muted;
    if (ui.selectPending.Active() && ui.selectPending.itemId == it.id) {
        sub = T("выбираю на принтере…", "selecting on the printer…");
        subColor = C::accent;
    } else if (current) {
        sub = Format1(T("выбрано на принтере · %s", "on the printer · %s"), sub);
        subColor = C::accent;
    } else if (ui.selectMessage.IsFor(it.id)) {
        sub = ui.selectMessage.text;
        subColor = ui.selectMessage.ok ? C::accent : C::warn;
    }
    dl->AddText(BodyFont(), 14, {textX, p.y + 36}, Col(subColor), sub.c_str(), nullptr, textW);
    dl->PopClipRect();
    if (toCatalog || (clicked && !researched)) OpenInCatalog(ui, it.id);
    else if (clicked && !current) SendSelect(ui, it);
    ImGui::PopID();
}

// "Смола 1/2" for each ingredient, coloured by whether the player has enough; "2× Смола" when the
// count is unknown. Pills that do not fit before maxX are left out.
void DrawIngredientPills(ImDrawList* dl, ImVec2 p, const Item& item, const Live& live, float maxX) {
    float x = kStripNameX;
    const std::vector<Ingredient>& ingredients = IngredientsOf(item);
    for (size_t k = 0; k < ingredients.size(); ++k) {
        const Ingredient& ingredient = ingredients[k];
        std::string label = IngredientLabel(ingredient);
        uint32_t fg = C::muted;
        uint32_t bg = C::accentSoft;
        if (k < live.printerHave.size() && live.printerHave[k].has_value()) {
            const int have = *live.printerHave[k];
            const bool enough = have >= ingredient.count;
            label = ingredient.name + " " + std::to_string(have) + "/" + std::to_string(ingredient.count);
            fg = enough ? C::accent : C::warn;
            bg = enough ? C::accentSoft : C::warnSoft;
        }
        const float w = TextWidth(BodyFont(), 14, label);
        if (x + w + 12 > maxX) break;
        dl->AddRectFilled(p + ImVec2(x, 34), p + ImVec2(x + w + 12, 56), Col(bg), 3);
        dl->AddText(BodyFont(), 14, p + ImVec2(x + 6, 37), Col(fg), label.c_str());
        x += w + 18;
    }
}

// The strip's status text: progress of a select, the last print result for the recipe on the
// printer (for a few seconds), or a hint when the printer says it cannot print.
bool StripMessage(const UiState& ui, const Item* currentItem, const Live& live, std::string& text, bool& ok) {
    const ItemMessage& printed = ui.printMessage;
    if (ui.selectPending.Active()) {
        text = T("выбираю…", "selecting…");
        ok = true;
    } else if (currentItem && printed.IsFor(currentItem->id) &&
               std::chrono::duration<double>(Clock::now() - printed.at).count() < kPrintMessageSec) {
        text = printed.text;
        ok = printed.ok;
    } else if (currentItem && !live.printerCanPrint) {
        text = T("Не хватает ресурсов или энергии", "Not enough resources or power");
        ok = false;
    } else {
        return false;
    }
    return true;
}

void DrawPrintStrip(UiState& ui, int printerIndex, const Live& live) {
    const Item* currentItem = nullptr;
    for (const Item& it : Items()) {
        if (printerIndex >= 0 && it.printerIndex == printerIndex && IsCurrentRecipe(it, live)) currentItem = &it;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float ww = ImGui::GetWindowWidth();
    const float buttonW = std::min(190.0f, ww * 0.3f);
    const float stripW = ww - 2 * kPad;
    dl->AddRectFilled(p, p + ImVec2(stripW, kStripH), Col(C::panel), 3);

    std::string message;
    bool messageOk = false;
    const bool hasMessage = StripMessage(ui, currentItem, live, message, messageOk);
    const float messageRight = stripW - buttonW - 16;
    float textRight = 0;  // where the left-hand text ends; the message starts after it
    dl->PushClipRect(p, p + ImVec2(stripW - buttonW, kStripH), true);
    if (currentItem) {
        DrawIcon(dl, currentItem->icon, p + ImVec2(12, 16), 32, C::iconActive);
        // The name and the message share the first line; a long name gives way down to a minimum.
        float nameMaxW = messageRight - kStripNameX;
        if (hasMessage) nameMaxW = std::max(kStripMinNameW, nameMaxW - kStripGap - TextWidth(BodyFont(), 14, message));
        const std::string name = FitText(BoldFont(), 16, NameOf(*currentItem), nameMaxW);
        dl->AddText(BoldFont(), 16, p + ImVec2(kStripNameX, 8), Col(C::fg), name.c_str());
        textRight = kStripNameX + TextWidth(BoldFont(), 16, name);
        DrawIngredientPills(dl, p, *currentItem, live, stripW - buttonW - 10);
    } else {
        const char* text = live.connected ? T("Рецепт на принтере не распознан", "The printer's recipe is not recognised") : T("Нет связи с модом", "No connection to the mod");
        dl->AddText(BodyFont(), 15, p + ImVec2(14, 22), Col(C::muted), text);
        textRight = 14 + TextWidth(BodyFont(), 15, text);
    }
    if (hasMessage) {
        const std::string shown = FitText(BodyFont(), 14, message, messageRight - textRight - kStripGap);
        const float shownW = TextWidth(BodyFont(), 14, shown);
        dl->AddText(BodyFont(), 14, p + ImVec2(messageRight - shownW, 8), Col(messageOk ? C::accent : C::warn), shown.c_str());
    }
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(p + ImVec2(stripW - buttonW, 0));
    // Also disabled while a select steps through the printer's recipes: printing then would start
    // whichever recipe the scan is passing.
    ImGui::BeginDisabled(!currentItem || ui.printPending.Active() || ui.selectPending.Active());
    if (PrimaryButton(T("ПЕЧАТАТЬ", "PRINT"), {buttonW, kStripH}) && currentItem) SendPrint(ui, *currentItem);
    ImGui::EndDisabled();
}

struct Row {
    const Item* item;
    Status status;
};

// Everything the printer can make: researched first (by name), then locked (affordable first, by
// cost), each split by the catalog's purpose groups.
void DrawPrinterList(UiState& ui, int printerIndex, const Live& live) {
    const std::wstring query = Lower(Widen(ui.printerSearch));
    std::vector<Row> ready;
    std::vector<Row> locked;
    for (const Item& it : Items()) {
        if (it.printerIndex != printerIndex || (!query.empty() && it.searchText.find(query) == std::wstring::npos)) continue;
        const Status status = StatusOf(it, live);
        (status == Status::Researched ? ready : locked).push_back({&it, status});
    }
    std::sort(ready.begin(), ready.end(), [](const Row& a, const Row& b) { return NameOf(*a.item) < NameOf(*b.item); });
    // Affordable first, then too expensive, then rewards (not for sale); cheapest first within each.
    const auto rank = [](Status status) { return status == Status::Affordable ? 0 : status == Status::TooExpensive ? 1 : 2; };
    std::stable_sort(locked.begin(), locked.end(), [&](const Row& a, const Row& b) {
        return rank(a.status) != rank(b.status) ? rank(a.status) < rank(b.status) : a.item->cost < b.item->cost;
    });
    const float avail = ImGui::GetContentRegionAvail().x - 4;
    const int cols = std::max(1, (int)((avail + kGap) / (kRowMinW + kGap)));
    const float rowW = (avail - kGap * (cols - 1)) / cols;
    const auto section = [&](const char* title, const std::vector<Row>& rows) {
        if (rows.empty()) return;
        Caps(title, 22, C::accent);
        ImGui::SameLine(0, 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
        ImGui::TextColored(Vec(C::muted), "%d", (int)rows.size());
        for (int g = 0; g < (int)Groups().size(); ++g) {
            int n = 0;
            for (const Row& row : rows) {
                if (row.item->groupIndex != g) continue;
                if (n == 0) Caps(LabelOf(Groups()[g]).c_str(), 14, C::muted);
                if (n++ % cols) ImGui::SameLine(0, kGap);
                PrinterRow(ui, *row.item, row.status, rowW, live);
            }
            if (n) ImGui::Dummy({0, 4});
        }
        ImGui::Dummy({0, 10});
    };
    section(T("Можно печатать", "Ready to print"), ready);
    section(T("Не изучено", "Not researched"), locked);
    if (ready.empty() && locked.empty()) ImGui::TextColored(Vec(C::muted), "%s", T("Ничего не нашлось.", "Nothing found."));
}

}  // namespace

void DrawPrinterWindow(UiState& ui, const Live& live) {
    const std::string& category = ui.view.PrinterCategory();
    const int printerIndex = FindPrinterByCategory(category);
    // Stationary printers: right half by default. The backpack printer: a narrow column on the
    // left, next to the raised backpack. Separate window names keep separate saved layouts.
    const ImGuiIO& io = ImGui::GetIO();
    const bool backpack = category == kBackpackCategory;
    const float w = backpack ? std::max(440.0f, std::floor(io.DisplaySize.x * 0.28f)) : std::floor(io.DisplaySize.x * 0.5f);
    ImGui::SetNextWindowPos({backpack ? 0 : io.DisplaySize.x - w, 0}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({w, io.DisplaySize.y}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(kPrinterMinSize, {FLT_MAX, FLT_MAX});
    ImGui::Begin(backpack ? "Byteboard · Backpack" : "Byteboard · Printer", nullptr, kWindowFlags);
    const std::string title = printerIndex >= 0 ? LabelOf(Printers()[printerIndex]) : std::string(T("Принтер", "Printer"));
    if (DrawHeader(title, nullptr, live, false)) ui.view.OnCloseButton();
    ImGui::SetCursorPos({kPad, kHeaderH + 14});
    DrawPrintStrip(ui, printerIndex, live);
    ImGui::SetCursorPos({kPad, ImGui::GetCursorPosY() + 10});
    SearchBox("##pq", T("Поиск по названию или ресурсу…", "Search by name or resource…"), ui.printerSearch, sizeof ui.printerSearch);
    ImGui::SetCursorPos({kPad, ImGui::GetCursorPosY() + 8});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("plist", {-kPad, -kPad}, ImGuiChildFlags_None);
    if (printerIndex >= 0) DrawPrinterList(ui, printerIndex, live);
    else ImGui::TextColored(Vec(C::muted), T("Этот принтер не распознан (%s).", "This printer is not recognised (%s)."), category.c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
}

}  // namespace rd
