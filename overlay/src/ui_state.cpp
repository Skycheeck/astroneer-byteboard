#include "ui_state.h"
#include "lang.h"

#include "util.h"

namespace rd {

namespace {

using Clock = std::chrono::steady_clock;

double SecondsSince(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}

bool Expired(const Pending& p) {
    return SecondsSince(p.sentAt) > p.timeoutSec;
}

// "10 секунд", "1 секунду", "3 секунды" / "10 seconds", "1 second"
std::string Seconds(double value) {
    const long long n = (long long)value;
    if (UiLang() != Lang::Ru) return std::to_string(n) + " s";
    const long long lastTwo = n % 100;
    const long long last = n % 10;
    const char* word = "секунд";
    if (lastTwo < 11 || lastTwo > 14) {
        if (last == 1) word = "секунду";
        else if (last >= 2 && last <= 4) word = "секунды";
    }
    return std::to_string(n) + " " + word;
}

// The text for a result code of results.json (docs/PROTOCOL.md).
std::string ResultText(const CommandResult& r) {
    struct Entry {
        const char* code;
        const char* ru;
        const char* en;
    };
    static const Entry kTexts[] = {
        {"bad_command", "Мод не понял команду.", "The mod did not understand the command."},
        {"unknown_op", "Мод не знает такой команды.", "The mod does not know this command."},
        {"expired", "Команда устарела: мод получил её слишком поздно.", "The command expired: the mod got it too late."},
        {"not_in_world", "Мир ещё не загружен.", "No world is loaded yet."},
        {"disabled", "Изучение выключено: отметьте «Изучение разрешено» вверху.", "Research is off: tick \"Research allowed\" at the top."},
        {"creative_only", "Этот предмет есть только в творческом режиме.", "This item exists only in Creative Mode."},
        {"not_for_sale", "Этот предмет открывает задание или событие, а не байты.", "A mission or an event unlocks this item, not bytes."},
        {"already_unlocked", "Уже изучено в игре.", "Already researched in the game."},
        {"class_not_loaded", "Игра ещё не загрузила этот предмет.", "The game has not loaded this item yet."},
        {"no_printer", "Принтер не открыт.", "No printer is open."},
        {"busy", "Принтер ещё переключает рецепт.", "The printer is still switching recipes."},
        {"not_found", "Этого рецепта нет на принтере.", "This printer does not have this recipe."},
        {"printer_closed", "Принтер закрыли, пока выбирался рецепт.", "The printer was closed while the recipe was being selected."},
        {"printer_changed", "Открыли другой принтер, пока выбирался рецепт.", "Another printer was opened while the recipe was being selected."},
        {"superseded", "Выбран другой рецепт.", "Another recipe was selected."},
        {"timeout", "Рецепт не нашёлся за отведённое время.", "The recipe was not found in time."},
        {"no_response", "Принтер перестал отвечать.", "The printer stopped responding."},
        {"wrong_recipe", "На принтере выбран другой рецепт.", "The printer shows a different recipe."},
        {"cannot_print", "Не хватает ресурсов или энергии.", "Not enough resources or power."},
    };
    for (const Entry& e : kTexts) {
        if (r.code == e.code) return T(e.ru, e.en);
    }
    if (r.code == "call_failed") return Format1(T("Ошибка в игре: %s", "Game error: %s"), r.error);
    return Format1(T("Не получилось: %s", "Failed: %s"), r.error.empty() ? r.code : r.error);
}

// The print strip is a single short line: its messages have no final period.
std::string StripText(std::string text) {
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

void Finish(Pending& pending, ItemMessage& channel, std::string text, bool ok) {
    channel = {pending.itemId, std::move(text), ok, Clock::now()};
    pending = {};
}

void Send(CommandOp op, const Item& item, Pending& pending, ItemMessage& channel, double timeoutSec) {
    const SendResult sent = WriteCommand(op, item.classPath);
    switch (sent.status) {
        case SendResult::Status::Sent:
            pending = {item.id, sent.commandId, Clock::now(), timeoutSec};
            channel = {};
            break;
        case SendResult::Status::Busy:
            channel = {item.id, T("Предыдущая команда ещё не обработана.", "The previous command has not been handled yet."), false, Clock::now()};
            break;
        case SendResult::Status::IoError:
            channel = {item.id, T("Не удалось записать команду.", "Could not write the command."), false, Clock::now()};
            break;
    }
}

void ResolveUnlock(UiState& ui, const Live& live) {
    Pending& p = ui.unlockPending;
    if (!p.Active()) return;
    const Item* item = FindItem(p.itemId);
    const CommandResult* result = FindResult(live, p.commandId);
    if (item && IsResearched(*item, live)) {
        Finish(p, ui.unlockMessage, T("Изучено в игре.", "Researched in the game."), true);
    } else if (result && !result->ok) {
        Finish(p, ui.unlockMessage, ResultText(*result), result->code == "already_unlocked");
    } else if (Expired(p)) {
        // An ok result only means the request reached the game; success is the unlocked list.
        Finish(p, ui.unlockMessage, Format1(T("Игра не подтвердила изучение за %s.", "The game did not confirm the research within %s."), Seconds(p.timeoutSec)), false);
    }
}

void ResolveSelect(UiState& ui, const Live& live) {
    // A select message is stale once its item is the printer's recipe after all.
    if (!ui.selectMessage.text.empty()) {
        const Item* item = FindItem(ui.selectMessage.itemId);
        if (item && IsCurrentRecipe(*item, live)) ui.selectMessage = {};
    }
    Pending& p = ui.selectPending;
    if (!p.Active()) return;
    const Item* item = FindItem(p.itemId);
    const CommandResult* result = FindResult(live, p.commandId);
    if (item && IsCurrentRecipe(*item, live)) {
        p = {};  // the printer shows the recipe: done, whatever the result will say
    } else if (result && result->ok) {
        p = {};
    } else if (result) {
        Finish(p, ui.selectMessage, ResultText(*result), false);
    } else if (Expired(p)) {
        Finish(p, ui.selectMessage, Format1(T("Мод не ответил за %s.", "The mod did not answer within %s."), Seconds(p.timeoutSec)), false);
    }
}

void ResolvePrint(UiState& ui, const Live& live) {
    Pending& p = ui.printPending;
    if (!p.Active()) return;
    if (const CommandResult* result = FindResult(live, p.commandId)) {
        Finish(p, ui.printMessage, result->ok ? std::string(T("Печать запущена", "Printing started")) : StripText(ResultText(*result)), result->ok);
    } else if (Expired(p)) {
        Finish(p, ui.printMessage, T("Мод не ответил", "The mod did not answer"), false);
    }
}

}  // namespace

bool IsResearched(const Item& item, const Live& live) {
    return live.unlocked.count(item.classKey) != 0;
}

Status StatusOf(const Item& item, const Live& live) {
    if (IsResearched(item, live)) return Status::Researched;
    if (item.unlock != Unlock::Research) return Status::Reward;
    if (item.cost == 0 && !live.connected) return Status::Researched;
    return item.cost <= (long long)live.balance ? Status::Affordable : Status::TooExpensive;
}

bool IsCurrentRecipe(const Item& item, const Live& live) {
    if (live.printerCurrent.empty()) return false;
    return live.printerCurrent == item.pickupKey || live.printerCurrent == item.classKey;
}

void ViewState::Show(Mode mode, bool followsGame) {
    mode_ = mode;
    followsGame_ = followsGame;
    visible_ = true;
}

void ViewState::Hide() {
    visible_ = false;
    followsGame_ = false;
}

void ViewState::HideCatalog() {
    userCatalog_ = false;
    if (gamePrinterOpen_) Show(Mode::Printer, true);
    else Hide();
}

void ViewState::LeavePrinter() {
    if (gameCatalogOpen_) Show(Mode::Catalog, true);
    else if (userCatalog_) Show(Mode::Catalog, false);
    else Hide();
}

void ViewState::OnSignal(const Request& request) {
    switch (request.kind) {
        case RequestKind::CatalogOpen:
            gameCatalogOpen_ = true;
            Show(Mode::Catalog, true);
            break;
        case RequestKind::CatalogClosed:
            gameCatalogOpen_ = false;
            // A close never hides a view it does not belong to.
            if (visible_ && mode_ == Mode::Catalog && followsGame_) {
                if (userCatalog_) Show(Mode::Catalog, false);
                else HideCatalog();
            }
            break;
        case RequestKind::PrinterOpen:
            gamePrinterOpen_ = true;
            printerCategory_ = request.category;
            Show(Mode::Printer, true);
            break;
        case RequestKind::PrinterClosed:
            gamePrinterOpen_ = false;
            if (visible_ && mode_ == Mode::Printer && followsGame_) LeavePrinter();
            break;
    }
}

void ViewState::OnEscape() {
    if (!visible_) return;
    if (mode_ == Mode::Catalog) {
        HideCatalog();
        return;
    }
    // Hiding the printer list hides the whole overlay: a catalog the user had opened before must
    // not come back when this printer closes.
    userCatalog_ = false;
    Hide();
}

void ViewState::OnCloseButton() {
    OnEscape();
}

void ViewState::OpenUserCatalog() {
    userCatalog_ = true;
    Show(Mode::Catalog, false);
}

UiState& Ui() {
    static UiState state;
    return state;
}

void ApplyRequests(UiState& ui, const std::vector<Request>& requests) {
    for (const Request& r : requests) {
        Log(std::string("signal ") + Name(r.kind) + (r.category.empty() ? "" : " " + r.category));
        ui.dropFocus = true;  // a key that opened something must not land in a search box
        ui.view.OnSignal(r);
        if (r.kind == RequestKind::PrinterOpen) {
            ui.printerSearch[0] = 0;
            ui.printMessage = {};
            ui.selectMessage = {};
        }
    }
}

void ResolvePending(UiState& ui, const Live& live) {
    ResolveUnlock(ui, live);
    ResolveSelect(ui, live);
    ResolvePrint(ui, live);
}

void OpenInCatalog(UiState& ui, const std::string& itemId) {
    ui.view.OpenUserCatalog();
    ui.selectedId = itemId;
    ui.filter = {};
    ui.scrollToSelected = true;
}

void SendUnlock(UiState& ui, const Item& item) {
    Send(CommandOp::Unlock, item, ui.unlockPending, ui.unlockMessage, kUnlockTimeoutSec);
}

void SendSelect(UiState& ui, const Item& item) {
    Send(CommandOp::Select, item, ui.selectPending, ui.selectMessage, kSelectTimeoutSec);
}

void SendPrint(UiState& ui, const Item& item) {
    Send(CommandOp::Print, item, ui.printPending, ui.printMessage, kPrintTimeoutSec);
    ui.printMessage.text = StripText(ui.printMessage.text);
}

}  // namespace rd
