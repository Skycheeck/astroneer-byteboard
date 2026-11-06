// The research catalog window: search, filter chips, the card grid and the detail panel.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "ui_widgets.h"
#include "ui_windows.h"
#include "util.h"

namespace rd {

namespace {

constexpr ImVec2 kCatalogMinSize{620, 480};
constexpr ImGuiWindowFlags kWindowFlags =
    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

// What changes from frame to frame for each item, parallel to Items().
struct CatalogView {
    std::vector<Status> status;
    std::vector<char> matchesQuery;
};

CatalogView BuildView(const UiState& ui, const Live& live) {
    CatalogView view;
    const std::wstring query = Lower(Widen(ui.filter.search));
    const std::vector<Item>& items = Items();
    view.status.reserve(items.size());
    view.matchesQuery.reserve(items.size());
    for (const Item& it : items) {
        view.status.push_back(StatusOf(it, live));
        view.matchesQuery.push_back(query.empty() || it.searchText.find(query) != std::wstring::npos);
    }
    return view;
}

bool StatusMatches(Status status, StatusFilter filter) {
    switch (filter) {
        case StatusFilter::All:
            return true;
        case StatusFilter::Affordable:
            return status == Status::Affordable;
        case StatusFilter::TooExpensive:
            return status == Status::TooExpensive;
        case StatusFilter::Researched:
            return status == Status::Researched;
    }
    return true;
}

// Each chip counts the items that pass the other two filters.
bool Passes(const CatalogView& view, size_t i, int group, int printer, StatusFilter status) {
    const Item& it = Items()[i];
    return view.matchesQuery[i] && (group < 0 || it.groupIndex == group) && (printer < 0 || it.printerIndex == printer) &&
           StatusMatches(view.status[i], status);
}

struct StatusChip {
    StatusFilter filter;
    const char* label;
};
constexpr StatusChip kStatusChips[] = {
    {StatusFilter::All, "Все"},
    {StatusFilter::Affordable, "Хватает байтов"},
    {StatusFilter::TooExpensive, "Не хватает"},
    {StatusFilter::Researched, "Изучено"},
};

void DrawFilters(CatalogFilter& filter, const CatalogView& view) {
    const auto count = [&](int group, int printer, StatusFilter status) {
        int n = 0;
        for (size_t i = 0; i < view.status.size(); ++i) n += Passes(view, i, group, printer, status);
        return n;
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {8, 4});
    ImGui::Dummy({0, 2});
    {
        ChipRow row("Назначение");
        if (row.Chip("g-all", "Все", count(-1, filter.printer, filter.status), filter.group < 0)) filter.group = -1;
        for (int g = 0; g < (int)Groups().size(); ++g) {
            const std::string id = "g" + std::to_string(g);
            const bool selected = filter.group == g;
            if (row.Chip(id.c_str(), Groups()[g].label, count(g, filter.printer, filter.status), selected)) filter.group = selected ? -1 : g;
        }
    }
    {
        ChipRow row("Где печатать");
        if (row.Chip("p-all", "Все", count(filter.group, -1, filter.status), filter.printer < 0)) filter.printer = -1;
        for (int p = 0; p < (int)Printers().size(); ++p) {
            const std::string id = "p" + std::to_string(p);
            const bool selected = filter.printer == p;
            if (row.Chip(id.c_str(), Printers()[p].label, count(filter.group, p, filter.status), selected)) filter.printer = selected ? -1 : p;
        }
    }
    {
        ChipRow row("Статус");
        for (size_t s = 0; s < std::size(kStatusChips); ++s) {
            const StatusChip& chip = kStatusChips[s];
            const std::string id = "s" + std::to_string(s);
            if (row.Chip(id.c_str(), chip.label, count(filter.group, filter.printer, chip.filter), filter.status == chip.filter)) {
                filter.status = chip.filter;
            }
        }
    }
    ImGui::PopStyleVar();
}

void Card(UiState& ui, const Item& it, Status status, float w) {
    ImGui::PushID(it.id.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size{w, kCardH};
    const bool clicked = ImGui::InvisibleButton("card", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = ui.selectedId == it.id;
    const bool researched = status == Status::Researched;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, p + size, Col(researched ? C::panelAlt : C::panel), 3);
    if (selected) dl->AddRect(p + ImVec2(1, 1), p + size - ImVec2(1, 1), Col(C::accent), 3.0f, ImDrawFlags_None, 2.0f);
    else dl->AddRect(p, p + size, Col(hovered ? C::faint : C::line), 3.0f);
    const float tile = 48;
    const ImVec2 tp = p + ImVec2(11, (kCardH - tile) / 2);
    dl->AddRectFilled(tp, tp + ImVec2(tile, tile), Col(researched ? C::panel : C::panelAlt), 3);
    DrawIcon(dl, it.icon, tp + ImVec2(8, 8), 32, IconTint(status, IconContext::Catalog));
    const float textX = tp.x + tile + 11;
    const float textW = p.x + w - 12 - textX;
    dl->PushClipRect(p, p + size, true);
    dl->AddText(BoldFont(), 15.5f, {textX, p.y + 9}, Col(researched ? C::muted : C::fg), it.nameRu.c_str(), nullptr, textW);
    const std::string cost = CostText(it, status);
    const float footerY = p.y + kCardH - 9 - 15;
    dl->AddText(BoldFont(), 14, {textX, footerY}, Col(status == Status::Affordable ? C::accent : C::muted), cost.c_str());
    const float costW = TextWidth(BoldFont(), 14, cost);
    dl->AddText(BodyFont(), 14, {textX + costW + 8, footerY}, Col(C::faint), Printers()[it.printerIndex].shortLabel.c_str());
    dl->PopClipRect();
    if (clicked) ui.selectedId = it.id;
    if (selected && ui.scrollToSelected) {
        ImGui::SetScrollHereY(0.3f);
        ui.scrollToSelected = false;
    }
    ImGui::PopID();
}

// Card grid, grouped by purpose and subgroup.
void DrawGrid(UiState& ui, const CatalogView& view) {
    const CatalogFilter& filter = ui.filter;
    const std::vector<Item>& items = Items();
    const float avail = ImGui::GetContentRegionAvail().x - 4;
    const int cols = std::max(1, (int)((avail + kGap) / (kCardMinW + kGap)));
    const float cardW = (avail - kGap * (cols - 1)) / cols;
    int shown = 0;
    for (int g = 0; g < (int)Groups().size(); ++g) {
        std::vector<size_t> rows;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].groupIndex == g && Passes(view, i, filter.group, filter.printer, filter.status)) rows.push_back(i);
        }
        if (rows.empty()) continue;
        shown += (int)rows.size();
        long long left = 0;
        for (size_t i : rows) {
            if (view.status[i] != Status::Researched) left += items[i].cost;
        }
        const std::string& label = Groups()[g].label;
        Caps(label.c_str(), 22, C::accent);
        ImGui::SameLine(0, 12);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        ImGui::TextColored(Vec(C::muted), "%d · осталось %s Б", (int)rows.size(), FormatInt(left).c_str());
        std::vector<std::string> subgroups;
        for (size_t i : rows) {
            if (std::find(subgroups.begin(), subgroups.end(), items[i].subgroup) == subgroups.end()) subgroups.push_back(items[i].subgroup);
        }
        for (const std::string& subgroup : subgroups) {
            if (subgroups.size() > 1 || subgroup != label) Caps(subgroup.c_str(), 14, C::muted);
            int n = 0;
            for (size_t i : rows) {
                if (items[i].subgroup != subgroup) continue;
                if (n++ % cols) ImGui::SameLine(0, kGap);
                Card(ui, items[i], view.status[i], cardW);
            }
        }
        ImGui::Dummy({0, 12});
    }
    if (!shown) {
        ImGui::Dummy({0, 40});
        ImGui::TextColored(Vec(C::muted), "Ничего не нашлось. Попробуйте другое слово или снимите фильтр.");
    }
    ui.scrollToSelected = false;  // the selected item is not in the grid (filtered out or unknown)
}

// One chip per ingredient; clicking one searches for everything made from it.
void DrawRecipeChips(UiState& ui, const Item& it) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Flow flow(6, 0, -1);
    for (const Ingredient& ingredient : it.ingredients) {
        const std::string label = IngredientLabel(ingredient);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str()) + ImVec2(16, 10);
        flow.Next(size.x);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton(label.c_str(), size)) {
            ui.filter = {};
            snprintf(ui.filter.search, sizeof ui.filter.search, "%s", ingredient.name.c_str());
        }
        dl->AddRectFilled(p, p + size, Col(ImGui::IsItemHovered() ? C::accentSoft : C::panelAlt), 3);
        dl->AddText(p + ImVec2(8, 5), Col(C::fg), label.c_str());
    }
}

// The same subgroup at every printer, cheapest first; a click selects that item.
void DrawKin(UiState& ui, const Item& selected, const Live& live) {
    std::vector<const Item*> kin;
    for (const Item& it : Items()) {
        if (it.groupIndex == selected.groupIndex && it.subgroup == selected.subgroup) kin.push_back(&it);
    }
    if (kin.size() < 2) return;
    ImGui::Spacing();
    Caps((selected.subgroup + ": все принтеры").c_str(), 14, C::accent);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const Item* k : kin) {
        ImGui::PushID(k->id.c_str());
        const bool isSelected = k == &selected;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("kin", {w, 30})) ui.selectedId = k->id;
        if (isSelected || ImGui::IsItemHovered()) dl->AddRectFilled(p, p + ImVec2(w, 30), Col(isSelected ? C::accentSoft : C::panelAlt), 3);
        if (isSelected) dl->AddRectFilled(p, p + ImVec2(3, 30), Col(C::accent));
        dl->AddText(BodyFont(), 15, p + ImVec2(10, 6), Col(C::fg), k->nameRu.c_str());
        const float nameW = TextWidth(BodyFont(), 15, k->nameRu);
        dl->AddText(BodyFont(), 13, p + ImVec2(16 + nameW, 8), Col(C::faint), Printers()[k->printerIndex].shortLabel.c_str());
        const Status status = StatusOf(*k, live);
        const std::string cost = CostText(*k, status);
        const float costW = TextWidth(BoldFont(), 14, cost);
        dl->AddText(BoldFont(), 14, p + ImVec2(w - 10 - costW, 7), Col(status == Status::Affordable ? C::accent : C::muted), cost.c_str());
        ImGui::PopID();
    }
}

void DrawResearchButton(UiState& ui, const Item& selected, Status status, const Live& live) {
    if (status != Status::Researched) {
        const Pending& pending = ui.unlockPending;
        const bool busy = pending.Active();
        ImGui::BeginDisabled(!(live.connected && live.allowUnlock && status == Status::Affordable && !busy));
        if (PrimaryButton(Upper("Изучить за " + FormatInt(selected.cost) + " Б"), {-FLT_MIN, 44})) SendUnlock(ui, selected);
        ImGui::EndDisabled();
        if (busy && pending.itemId == selected.id) {
            Wrapped(C::muted, "Команда отправлена в игру, жду подтверждения…");
        } else if (busy) {
            const Item* other = FindItem(pending.itemId);
            Wrapped(C::muted, "Ждём подтверждения изучения «" + (other ? other->nameRu : pending.itemId) + "»…");
        } else if (!live.connected) {
            Wrapped(C::warn, "Нет связи с Lua-модом Byteboard.");
        } else if (!live.allowUnlock) {
            Wrapped(C::muted, "Изучение выключено: отметьте «Изучение разрешено» вверху.");
        } else if (status == Status::TooExpensive) {
            Wrapped(C::warn, "Не хватает " + FormatInt(selected.cost - (long long)live.balance) + " Б.");
        }
    }
    const ItemMessage& message = ui.unlockMessage;
    if (message.IsFor(selected.id)) Wrapped(message.ok ? C::accent : C::warn, message.text);
}

void DrawDetail(UiState& ui, const Live& live) {
    const Item* selected = FindItem(ui.selectedId);
    if (!selected) {
        Caps("Карточка", 16, C::accent);
        Wrapped(C::muted, "Выберите предмет, чтобы увидеть описание, рецепт и тот же предмет у других принтеров.");
        ImGui::Spacing();
        Wrapped(C::muted, "Поиск ищет и по ресурсам: «кварц» покажет всё, что из него печатается.");
        return;
    }
    const Status status = StatusOf(*selected, live);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, p + ImVec2(84, 84), Col(C::panelAlt), 3);
    DrawIcon(dl, selected->icon, p + ImVec2(12, 12), 60, IconTint(status, IconContext::Catalog));
    ImGui::SetCursorScreenPos(p + ImVec2(98, 4));
    ImGui::PushTextWrapPos(0);
    ImGui::PushFont(DisplayFont(), 22);
    ImGui::TextColored(Vec(C::accent), "%s", Upper(selected->nameRu).c_str());
    ImGui::PopFont();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 98);
    ImGui::TextColored(Vec(C::muted), "%s", selected->nameEn.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos({p.x, std::max(ImGui::GetCursorScreenPos().y, p.y + 84) + 12});

    KeyValue("Цена", CostText(*selected, status), status == Status::Affordable ? C::accent : C::fg);
    KeyValue("Печать", Printers()[selected->printerIndex].label);
    KeyValue("Группа", Groups()[selected->groupIndex].label + " › " + selected->subgroup);
    if (!selected->ingredients.empty()) {
        ImGui::Spacing();
        Caps("Рецепт", 14, C::accent);
        DrawRecipeChips(ui, *selected);
    }
    if (!selected->description.empty()) {
        ImGui::Spacing();
        Caps("Описание", 14, C::accent);
        Wrapped(C::fg, selected->description);
    }
    DrawKin(ui, *selected, live);
    ImGui::Spacing();
    ImGui::Spacing();
    DrawResearchButton(ui, *selected, status, live);
}

}  // namespace

void DrawCatalogWindow(UiState& ui, const Live& live) {
    const CatalogView view = BuildView(ui, live);
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({std::floor(io.DisplaySize.x * 0.5f), io.DisplaySize.y}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(kCatalogMinSize, {FLT_MAX, FLT_MAX});
    ImGui::Begin("Byteboard", nullptr, kWindowFlags);
    if (DrawHeader("Byteboard", "каталог исследований · F8 — закрыть", live, true)) ui.view.OnCloseButton();
    ImGui::SetCursorPos({kPad, kHeaderH + 16});
    SearchBox("##q", "Название, ресурс или назначение: «батарея», «литий», «rover»…", ui.filter.search, sizeof ui.filter.search);
    DrawFilters(ui.filter, view);

    const float ww = ImGui::GetWindowWidth();
    ImGui::SetCursorPos({kPad, ImGui::GetCursorPosY() + 10});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    const float detailW = std::clamp(ww * 0.36f, 300.0f, 380.0f);
    ImGui::BeginChild("grid", {-(detailW + kPad + 14), -kPad}, ImGuiChildFlags_None);
    DrawGrid(ui, view);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SameLine(0, 14);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18, 16});
    ImGui::PushStyleColor(ImGuiCol_Border, Vec(C::line));
    ImGui::BeginChild("detail", {-kPad, -kPad}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    DrawDetail(ui, live);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::End();
}

}  // namespace rd
