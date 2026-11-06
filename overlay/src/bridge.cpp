#include "bridge.h"

#include <windows.h>

#include <charconv>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>

#include "json.h"
#include "paths.h"
#include "util.h"

namespace rd {

namespace {

// The mod replaces its files by delete + rename, so a read can briefly find nothing. The previous
// state is kept for this many polls (500 ms apart) before the overlay shows "no connection".
constexpr int kMissedStateReadsTolerated = 2;
constexpr size_t kMaxQueuedRequests = 32;
constexpr double kParseErrorLogIntervalSec = 60;

std::mutex g_liveMutex;
std::shared_ptr<const Live> g_live = std::make_shared<Live>();

std::mutex g_requestMutex;
std::vector<Request> g_requests;

void Publish(std::shared_ptr<const Live> live) {
    std::lock_guard lock(g_liveMutex);
    g_live = std::move(live);
}

void KeepStateOf(const Live& previous, Live& next) {
    next.connected = previous.connected;
    next.balance = previous.balance;
    next.unlocked = previous.unlocked;
    next.printerCurrent = previous.printerCurrent;
    next.printerHave = previous.printerHave;
    next.printerCanPrint = previous.printerCanPrint;
}

// "3,1,?" -> {3, 1, unknown}
std::vector<std::optional<int>> ParseCounts(std::string_view text) {
    std::vector<std::optional<int>> counts;
    if (Trim(text).empty()) return counts;
    for (std::string_view part : Split(text, ',')) {
        part = Trim(part);
        int value = 0;
        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), value);
        if (!part.empty() && ec == std::errc() && end == part.data() + part.size()) counts.push_back(value);
        else counts.push_back(std::nullopt);
    }
    return counts;
}

void ReadState(const Live& previous, Live& next) {
    static int missedReads = 0;  // poll thread only
    static RateLimitedLog parseErrors(kParseErrorLogIntervalSec);
    std::string text;
    double ageSec = 0;
    std::optional<json::Value> doc;
    if (ReadSharedFile(Paths().out / "state.json", text, &ageSec)) {
        std::string error;
        doc = json::Parse(text, &error);
        if (doc && !doc->IsObject()) error = "not an object";
        if (!error.empty()) {
            parseErrors.Write("state.json: " + error);
            doc.reset();
        }
    }
    if (!doc) {
        if (++missedReads <= kMissedStateReadsTolerated) KeepStateOf(previous, next);
        return;
    }
    missedReads = 0;
    const json::Value& state = *doc;
    next.connected = ageSec < kStateStaleSec && state["ok"].AsBool(false);
    next.balance = state["balance"].AsNumber(0);
    for (const json::Value& classPath : state["unlocked"].Items()) {
        if (classPath.GetType() == json::Type::String) next.unlocked.insert(ClassKey(classPath.AsString()));
    }
    next.printerCurrent = ClassKey(state["printerCurrent"].AsString());
    next.printerHave = ParseCounts(state["printerHave"].AsString());
    next.printerCanPrint = state["printerCanPrint"].AsBool(true);
}

void ReadResults(const Live& previous, Live& next) {
    static RateLimitedLog parseErrors(kParseErrorLogIntervalSec);  // poll thread only
    std::string text;
    if (!ReadSharedFile(Paths().out / "results.json", text)) {
        next.results = previous.results;
        return;
    }
    std::string error;
    const std::optional<json::Value> doc = json::Parse(text, &error);
    if (!doc || !doc->IsArray()) {
        parseErrors.Write("results.json: " + (doc ? std::string("not an array") : error));
        next.results = previous.results;
        return;
    }
    for (const json::Value& entry : doc->Items()) {
        if (!entry.IsObject()) continue;
        CommandResult r;
        r.id = entry["id"].AsString();
        r.op = entry["op"].AsString();
        r.arg = entry["arg"].AsString();
        r.ok = entry["ok"].AsBool(false);
        r.code = entry["code"].AsString();
        r.error = entry["error"].AsString();
        next.results.push_back(std::move(r));
    }
}

void PushRequest(Request request) {
    std::lock_guard lock(g_requestMutex);
    if (g_requests.size() < kMaxQueuedRequests) g_requests.push_back(std::move(request));
}

// One signal file: "<seq> open [<category>]" or "<seq> closed".
class SignalFile {
public:
    explicit SignalFile(const char* name) : name_(name) {}

    // True when the sequence number changed since the last call; the first read is only a baseline.
    // words receives the line split at spaces.
    bool Changed(std::vector<std::string>& words) {
        std::string line;
        ReadSharedFile(Paths().out / name_, line);
        words.clear();
        for (std::string_view w : Split(Trim(line), ' ')) {
            if (!w.empty()) words.emplace_back(w);
        }
        long long seq = 0;
        if (!words.empty()) std::from_chars(words[0].data(), words[0].data() + words[0].size(), seq);
        if (!started_) {
            started_ = true;
            lastSeq_ = seq;
            return false;
        }
        if (words.empty() || seq == lastSeq_) return false;  // missing for a moment, or unchanged
        lastSeq_ = seq;
        return true;
    }

private:
    const char* name_;
    bool started_ = false;
    long long lastSeq_ = 0;
};

}  // namespace

std::shared_ptr<const Live> SnapshotLive() {
    std::lock_guard lock(g_liveMutex);
    return g_live;
}

const CommandResult* FindResult(const Live& live, const std::string& commandId) {
    if (commandId.empty()) return nullptr;
    for (const CommandResult& r : live.results) {
        if (r.id == commandId) return &r;
    }
    return nullptr;
}

void PollState() {
    const std::shared_ptr<const Live> previous = SnapshotLive();
    auto next = std::make_shared<Live>();
    ReadResults(*previous, *next);
    ReadState(*previous, *next);
    // Read last, so it is current even while state.json is missing, and a click on the checkbox
    // (render thread) can only be overwritten by an older value in a very short window.
    next->allowUnlock = FileExists(Paths().in / "allow-unlock");
    next->polled = true;
    Publish(std::move(next));
}

const char* Name(RequestKind kind) {
    switch (kind) {
        case RequestKind::CatalogOpen:
            return "catalog open";
        case RequestKind::CatalogClosed:
            return "catalog closed";
        case RequestKind::PrinterOpen:
            return "printer open";
        case RequestKind::PrinterClosed:
            return "printer closed";
    }
    return "?";
}

void PollSignals() {
    static SignalFile catalog("catalog.txt");  // poll thread only
    static SignalFile printer("printer.txt");
    std::vector<std::string> words;
    if (catalog.Changed(words)) {
        const bool open = words.size() >= 2 && words[1] == "open";
        PushRequest({open ? RequestKind::CatalogOpen : RequestKind::CatalogClosed, ""});
    }
    if (printer.Changed(words)) {
        if (words.size() >= 3 && words[1] == "open") PushRequest({RequestKind::PrinterOpen, words[2]});
        else PushRequest({RequestKind::PrinterClosed, ""});
    }
}

std::vector<Request> TakeRequests() {
    std::vector<Request> taken;
    std::lock_guard lock(g_requestMutex);
    taken.swap(g_requests);
    return taken;
}

const char* Name(CommandOp op) {
    switch (op) {
        case CommandOp::Unlock:
            return "unlock";
        case CommandOp::Select:
            return "select";
        case CommandOp::Print:
            return "print";
    }
    return "?";
}

SendResult WriteCommand(CommandOp op, const std::string& classPath) {
    static unsigned counter = 0;  // render thread only
    SendResult result;
    const fs::path& in = Paths().in;
    std::error_code ec;
    fs::create_directories(in, ec);
    // "o" marks the overlay's commands; tick count and counter make the id unique in the session.
    char id[24];
    snprintf(id, sizeof id, "o%08llx%04x", (unsigned long long)(GetTickCount64() & 0xffffffff), ++counter & 0xffff);
    std::string line = std::string(id) + " " + std::to_string((long long)std::time(nullptr)) + " " + Name(op);
    if (!classPath.empty()) line += " " + classPath;
    const fs::path tmp = in / ("cmd." + std::string(id) + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << line << "\n";
        f.close();
        if (f.fail()) {
            fs::remove(tmp, ec);
            Log("cannot write " + Utf8(tmp));
            return result;
        }
    }
    // The move without MOVEFILE_REPLACE_EXISTING is the atomic "no command waiting" check, so a
    // command the mod has not run yet is never overwritten.
    if (!MoveFileExW(tmp.c_str(), (in / "cmd.txt").c_str(), 0)) {
        const DWORD error = GetLastError();
        fs::remove(tmp, ec);
        // ERROR_ACCESS_DENIED: the mod is deleting the previous cmd.txt right now.
        if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS || error == ERROR_ACCESS_DENIED) {
            result.status = SendResult::Status::Busy;
        } else {
            Log("cannot move the command to cmd.txt: Windows error " + std::to_string(error));
        }
        return result;
    }
    result.status = SendResult::Status::Sent;
    result.commandId = id;
    return result;
}

bool SetAllowUnlock(bool on) {
    const fs::path file = Paths().in / "allow-unlock";
    std::error_code ec;
    bool done = false;
    if (on) {
        fs::create_directories(Paths().in, ec);
        std::ofstream f(file, std::ios::binary | std::ios::trunc);  // an empty marker file
        f.close();
        done = !f.fail();
    } else {
        fs::remove(file, ec);
        done = !ec;
    }
    if (!done) {
        Log(std::string("cannot ") + (on ? "create " : "delete ") + Utf8(file));
        return false;
    }
    std::lock_guard lock(g_liveMutex);
    auto updated = std::make_shared<Live>(*g_live);
    updated->allowUnlock = on;
    g_live = std::move(updated);
    return true;
}

}  // namespace rd
