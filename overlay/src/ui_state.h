#pragma once
// Everything the overlay's UI remembers between frames: which view is shown (the view state
// machine), search and filters, the selected item, commands waiting for a result and the messages
// about them. Also the item status rules shared by both windows.
// Threads: render thread only (preview.exe calls in from the thread that presents its frames).
#include <chrono>
#include <string>
#include <vector>

#include "bridge.h"
#include "catalog.h"

namespace rd {

enum class Status { Researched, Affordable, TooExpensive };

bool IsResearched(const Item& item, const Live& live);
// Researched = in the game's unlocked list. A zero research cost does not mean researched (Large
// Platform C costs 0 but comes from elsewhere); it is only a guess while the game is not connected.
Status StatusOf(const Item& item, const Live& live);
// The open printer shows this item's recipe (docs/PROTOCOL.md: compared by object name).
bool IsCurrentRecipe(const Item& item, const Live& live);

enum class Mode { Catalog, Printer };

// Which overlay window is shown and why. The game's research catalog and a printer can be open at
// once: the last one opened is shown, and when one closes or is hidden the overlay falls back to
// the other while it is still open. A catalog the user opened (F8 or "open in catalog") stays
// until the user hides it; a view that follows a game panel closes with that panel.
class ViewState {
public:
    bool Visible() const { return visible_; }
    Mode GetMode() const { return mode_; }
    const std::string& PrinterCategory() const { return printerCategory_; }
    // True while Esc should hide the overlay's view. Otherwise Esc goes to the game, which closes
    // its panel, and the overlay follows the close signal.
    bool OwnsEscape() const { return visible_ && !followsGame_; }

    void OnSignal(const Request& request);
    void OnToggleKey();  // F8: hide the catalog, or bring it up (also over a printer list)
    void OnEscape();
    void OnCloseButton();
    void OpenUserCatalog();

private:
    void Show(Mode mode, bool followsGame);
    void Hide();
    void HideCatalog();   // back to the printer list while the game's printer is open
    void LeavePrinter();  // back to an open catalog, else hidden

    bool gameCatalogOpen_ = false;
    bool gamePrinterOpen_ = false;
    bool userCatalog_ = false;
    std::string printerCategory_;
    Mode mode_ = Mode::Catalog;
    bool visible_ = false;
    bool followsGame_ = false;
};

enum class StatusFilter { All, Affordable, TooExpensive, Researched };

struct CatalogFilter {
    char search[128] = "";
    int group = -1;    // index into Groups(), -1 = all
    int printer = -1;  // index into Printers(), -1 = all
    StatusFilter status = StatusFilter::All;
};

// A command sent to the mod and not answered yet.
struct Pending {
    std::string itemId;
    std::string commandId;
    std::chrono::steady_clock::time_point sentAt;
    double timeoutSec = 0;
    bool Active() const { return !commandId.empty(); }
};

// The last outcome of one kind of command, about one item.
struct ItemMessage {
    std::string itemId;
    std::string text;
    bool ok = false;
    std::chrono::steady_clock::time_point at;
    bool IsFor(const std::string& id) const { return !text.empty() && itemId == id; }
};

struct UiState {
    ViewState view;
    CatalogFilter filter;
    char printerSearch[128] = "";
    std::string selectedId;
    bool scrollToSelected = false;  // set when the printer list opens the catalog on an item
    bool dropFocus = false;         // clear ImGui focus on the next frame
    Pending unlockPending;
    Pending selectPending;
    Pending printPending;
    ItemMessage unlockMessage;  // shown on the catalog card of the item
    ItemMessage selectMessage;  // shown on the printer row of the item
    ItemMessage printMessage;   // shown in the print strip while the item is the printer's recipe
};

UiState& Ui();

// Applies the panel signals of the game, in order.
void ApplyRequests(UiState& ui, const std::vector<Request>& requests);
// Turns command results (and, for unlock and select, the game state) into messages.
void ResolvePending(UiState& ui, const Live& live);
// Opens the research catalog on one item: filters cleared, card selected and scrolled into view.
void OpenInCatalog(UiState& ui, const std::string& itemId);

void SendUnlock(UiState& ui, const Item& item);
void SendSelect(UiState& ui, const Item& item);
void SendPrint(UiState& ui, const Item& item);

// How long a print result stays in the print strip.
constexpr double kPrintMessageSec = 10.0;

}  // namespace rd
