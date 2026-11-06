#pragma once
// The overlay's side of the file protocol with the ResearchBridge Lua mod (docs/PROTOCOL.md):
// state.json and results.json, the catalog.txt/printer.txt signals, cmd.txt and allow-unlock.
// Threads: the overlay's poll thread calls PollState and PollSignals. The render thread reads
// snapshots, takes requests, writes commands and sets allow-unlock. Shared data is guarded here.
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace rd {

// Timings from docs/PROTOCOL.md.
constexpr double kStateStaleSec = 5.0;
constexpr double kUnlockTimeoutSec = 10.0;
constexpr double kPrintTimeoutSec = 10.0;
constexpr double kSelectTimeoutSec = 30.0;

// One entry of results.json.
struct CommandResult {
    std::string id;
    std::string op;
    std::string arg;
    bool ok = false;
    std::string code;   // machine-readable outcome ("ok", "no_printer", ...)
    std::string error;  // English detail for logs
};

// What the overlay knows about the game, rebuilt by every PollState.
struct Live {
    bool connected = false;  // state.json is fresh and says ok
    double balance = 0;
    std::unordered_set<std::string> unlocked;  // ClassKey of every unlocked item type
    std::vector<CommandResult> results;        // newest first
    bool allowUnlock = false;
    std::string printerCurrent;  // ClassKey of the open printer's recipe; empty when no printer is open
    std::vector<std::optional<int>> printerHave;  // per ingredient, in recipe order; empty optional = not readable
    bool printerCanPrint = true;
    bool polled = false;  // false until the poll thread has read the files once
};

// The latest state; never null. Cheap: the poll thread publishes a new immutable object.
std::shared_ptr<const Live> SnapshotLive();
const CommandResult* FindResult(const Live& live, const std::string& commandId);

// Poll thread: reads allow-unlock, results.json and state.json.
void PollState();

// Open/close signals of the game's panels, in the order they happened.
enum class RequestKind { CatalogOpen, CatalogClosed, PrinterOpen, PrinterClosed };
const char* Name(RequestKind kind);
struct Request {
    RequestKind kind = RequestKind::CatalogClosed;
    std::string category;  // PrinterOpen: "ItemCategory_SmFab", ...
};
// Poll thread: turns changes of catalog.txt and printer.txt into requests.
void PollSignals();
// Render thread: the requests queued since the last call.
std::vector<Request> TakeRequests();

enum class CommandOp { Unlock, Select, Print };
const char* Name(CommandOp op);
struct SendResult {
    enum class Status { Sent, Busy, IoError };
    Status status = Status::IoError;
    std::string commandId;  // set when Sent
};
// Render thread: writes in/cmd.txt unless a command is still waiting there (Busy).
SendResult WriteCommand(CommandOp op, const std::string& classPath);

// Render thread: creates or deletes in/allow-unlock. On success the snapshot shows the new value
// at once instead of after the next poll. Failures are logged.
bool SetAllowUnlock(bool on);

}  // namespace rd
