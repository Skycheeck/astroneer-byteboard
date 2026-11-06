-- The file side of the mod (docs/PROTOCOL.md): paths, JSON, atomic writes with retry, logs,
-- open/close signals, the command results ring and in/cmd.txt. No game object is touched here.
local M = {}

local RESULTS_KEPT = 8
local LOG_ROTATE_BYTES = 1024 * 1024
local THROTTLE_SECONDS = 60
local THROTTLE_TRACKED_LINES = 100
local LOGGED_LINE_MAX = 300
local MAX_ID_LENGTH = 32
local EVENTS_LOG = "events.log"
local COMMANDS_LOG = "commands.log"
local COMMAND_FILE = "cmd.txt"
local ALLOW_UNLOCK_FILE = "allow-unlock"
local UTF8_BOM = "^\239\187\191"

local tag = ""
M.OUT = nil  -- "<mod>/out/"
M.IN = nil   -- "<mod>/in/"

function M.init(modDir, consoleTag)
    M.OUT = modDir .. "/out/"
    M.IN = modDir .. "/in/"
    tag = consoleTag
end

function M.exists(path)
    local f = io.open(path, "r")
    if not f then
        return false
    end
    f:close()
    return true
end

function M.unlockAllowed()
    return M.exists(M.IN .. ALLOW_UNLOCK_FILE)
end

-- JSON -------------------------------------------------------------------------------------------
local function escapeString(s)
    return (s:gsub('[%c"\\]', function(c)
        if c == '"' then
            return '\\"'
        end
        if c == "\\" then
            return "\\\\"
        end
        return string.format("\\u%04x", c:byte())
    end))
end

-- Encodes strings, numbers, booleans and tables. A table with array items is an array (its other
-- keys are ignored), an empty table is [], any other table is an object with sorted keys, so the
-- output is stable. NaN and infinities become null.
function M.json(value)
    local kind = type(value)
    if kind == "table" then
        if #value > 0 or next(value) == nil then
            local items = {}
            for i = 1, #value do
                items[i] = M.json(value[i])
            end
            return "[" .. table.concat(items, ",") .. "]"
        end
        local keys = {}
        for key in pairs(value) do
            keys[#keys + 1] = key
        end
        table.sort(keys, function(a, b)
            return tostring(a) < tostring(b)
        end)
        local fields = {}
        for _, key in ipairs(keys) do
            fields[#fields + 1] = '"' .. escapeString(tostring(key)) .. '":' .. M.json(value[key])
        end
        return "{" .. table.concat(fields, ",") .. "}"
    end
    if kind == "string" then
        return '"' .. escapeString(value) .. '"'
    end
    if kind == "number" then
        if value ~= value or value == math.huge or value == -math.huge then
            return "null"
        end
        if math.type(value) == "integer" then
            return tostring(value)
        end
        return string.format("%.17g", value)
    end
    if kind == "boolean" then
        return tostring(value)
    end
    return "null"
end

-- Logs -------------------------------------------------------------------------------------------
local function appendLine(fileName, line)
    if not M.OUT then
        return
    end
    local f = io.open(M.OUT .. fileName, "a")
    if not f then
        return
    end
    f:write(os.date("%Y-%m-%d %H:%M:%S ") .. line .. "\n")
    f:close()
end

-- events.log: diagnostics (signals, printer panels, select jobs, errors).
function M.logEvent(line)
    appendLine(EVENTS_LOG, line)
end

-- commands.log: every command with its outcome, and every ServerRequestUnlockItem call.
function M.logCommand(line)
    appendLine(COMMANDS_LOG, line)
end

-- Logs a line at most once a minute; repeats in between are counted and the count is added to the
-- next copy. Keeps an error that fires every tick from filling the log.
local throttled = {}
local throttledCount = 0
function M.logThrottled(line)
    local now = os.time()
    local entry = throttled[line]
    if entry and now < entry.quietUntil then
        entry.suppressed = entry.suppressed + 1
        return
    end
    local suffix = ""
    if entry and entry.suppressed > 0 then
        suffix = " (also " .. entry.suppressed .. " times since the previous report)"
    end
    if not entry then
        if throttledCount >= THROTTLE_TRACKED_LINES then
            throttled = {}
            throttledCount = 0
        end
        throttledCount = throttledCount + 1
    end
    throttled[line] = { quietUntil = now + THROTTLE_SECONDS, suppressed = 0 }
    M.logEvent(line .. suffix)
end

-- Prints to the UE4SS console and log, and to events.log.
function M.say(message)
    print(tag .. message .. "\n")
    M.logEvent(message)
end

-- At load: a log larger than 1 MB becomes <name>.old.log (replacing the previous one).
function M.rotateLogs()
    for _, name in ipairs({ EVENTS_LOG, COMMANDS_LOG }) do
        local path = M.OUT .. name
        local f = io.open(path, "rb")
        if f then
            local size = f:seek("end")
            f:close()
            if size and size > LOG_ROTATE_BYTES then
                local oldPath = M.OUT .. (name:gsub("%.log$", ".old.log"))
                os.remove(oldPath)
                os.rename(path, oldPath)
            end
        end
    end
end

-- Atomic writes ----------------------------------------------------------------------------------
-- Writes <name>.tmp, deletes <name> and renames. Lua's rename does not overwrite on Windows, so a
-- reader can briefly find the file missing (the protocol allows that). Returns true on success.
function M.writeFile(name, text)
    local target = M.OUT .. name
    local tmp = target .. ".tmp"
    local f, openError = io.open(tmp, "wb")
    if not f then
        M.logThrottled("cannot write " .. name .. ": " .. tostring(openError))
        return false
    end
    local written, writeError = f:write(text)
    local closed, closeError = f:close()
    if not written or not closed then
        os.remove(tmp)
        M.logThrottled("cannot write " .. name .. ": " .. tostring(writeError or closeError))
        return false
    end
    os.remove(target)
    local renamed, renameError = os.rename(tmp, target)
    if not renamed then
        M.logThrottled("cannot replace " .. name .. ": " .. tostring(renameError))
        return false
    end
    return true
end

-- Files written once per event (results, signals) must not be lost when a reader briefly blocks
-- the replace: the latest text of a failed write is kept and retried by flushPending every fast
-- tick until it succeeds. state.json is rewritten every second anyway and does not use this.
local pending = {}

local function publish(name, text)
    if M.writeFile(name, text) then
        pending[name] = nil
    else
        pending[name] = text
    end
end

function M.flushPending()
    for name, text in pairs(pending) do
        if M.writeFile(name, text) then
            pending[name] = nil
        end
    end
end

-- Signals and results ----------------------------------------------------------------------------
-- "<seq> <line>": seq changes with every write; readers act when it changes.
local signalSeq = 0
function M.writeSignal(name, line)
    signalSeq = signalSeq + 1
    M.logEvent(name .. " " .. line)
    publish(name, (os.time() * 1000 + signalSeq) .. " " .. line .. "\n")
end

-- The last few results, newest first, so a late select result and the next command's result do
-- not overwrite each other. Readers look theirs up by id.
local results = {}
function M.writeResult(result)
    result.t = os.time()
    if result.ok then
        result.error = nil
    end
    table.insert(results, 1, result)
    while #results > RESULTS_KEPT do
        table.remove(results)
    end
    publish("results.json", M.json(results))
end

-- Commands ---------------------------------------------------------------------------------------
-- Parses "<id> <unixSec> <op> [<arg>]". The result always has line, id ("?" when the id itself is
-- unusable) and op ("" when missing); parseError is set when the line does not parse.
function M.parseCommand(line)
    local words = {}
    for word in line:gmatch("%S+") do
        words[#words + 1] = word
    end
    local cmd = { line = line:sub(1, LOGGED_LINE_MAX), id = "?", op = words[3] or "", arg = words[4] }
    local id = words[1]
    if not id or #id > MAX_ID_LENGTH or not id:match("^[0-9A-Za-z_%-]+$") then
        cmd.parseError = "bad command id"
        return cmd
    end
    cmd.id = id
    if #words < 3 or #words > 4 then
        cmd.parseError = "expected <id> <unixSec> <op> [<arg>]"
        return cmd
    end
    if not words[2]:match("^%d+$") then
        cmd.parseError = "bad unixSec: " .. words[2]
        return cmd
    end
    cmd.unixSec = tonumber(words[2])
    return cmd
end

-- Takes the pending command: reads in/cmd.txt and deletes it before anything runs, so a crash
-- cannot repeat it. If the delete fails the command is skipped (and retried next tick), otherwise
-- it would run again every tick.
function M.takeCommand()
    local path = M.IN .. COMMAND_FILE
    local f = io.open(path, "rb")
    if not f then
        return nil
    end
    local text = f:read("a") or ""
    f:close()
    local removed, removeError = os.remove(path)
    if not removed then
        M.logThrottled("cannot delete in/cmd.txt, command not run: " .. tostring(removeError))
        return nil
    end
    local withoutBom = text:gsub(UTF8_BOM, "")
    return M.parseCommand(withoutBom:match("^[^\r\n]*"))
end

-- At load: a cmd.txt left from an earlier session never runs.
function M.removeLeftoverCommand()
    local path = M.IN .. COMMAND_FILE
    if not M.exists(path) then
        return
    end
    local removed, removeError = os.remove(path)
    if removed then
        M.logEvent("removed a cmd.txt left from an earlier session")
    else
        M.logEvent("cannot remove the cmd.txt left from an earlier session: " .. tostring(removeError))
    end
end

return M
