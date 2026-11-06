-- ResearchBridge: the game side of Byteboard, a UE4SS Lua mod for ASTRONEER 1.42.3.
-- 1) State: once per second writes the byte balance, the unlocked list and the open printer's
--    recipe and ingredient counts to out/state.json.
-- 2) Signals: out/catalog.txt when the backpack research catalog opens or closes, out/printer.txt
--    when a printer panel or the backpack (Q) printer opens or closes. The overlay follows them.
-- 3) Commands: ten times a second from load, runs in/cmd.txt ("<id> <unixSec> <op> [<arg>]"):
--    unlock <class path> (ServerRequestUnlockItem, only while in/allow-unlock exists),
--    select <class path> (steps the open printer to that recipe), print [<class path>] (starts
--    the open printer). Outside a world every command gets not_in_world. Results go to
--    out/results.json.
-- 4) Loads the in-game overlay DLL (overlay/bin/ResearchOverlay.dll).
-- All file formats and timings: docs/PROTOCOL.md. Logs: out/events.log, out/commands.log.
-- It never calls Authority* or exec cheat functions.
--
-- Single-player only: on a multiplayer host, another player's printer panel, catalog or backpack
-- can be taken for the local player's (see rb/panels.lua). Commands use the local controller.
-- Needs RE-UE4SS experimental 3.0.1-1183-gcdbc0b66 or newer (LoopInGameThreadWithDelay).
--
-- Safety rules (each one cost a game crash before):
-- * Every game object is checked with valid() before any method call or property read. A call on
--   a null or stale object crashes inside UE4SS.dll, and pcall cannot catch that (rb/uobj.lua).
-- * No UObject is kept between ticks; objects are looked up again, only paths are stored.
-- * Only named properties are read; walking all properties of the player pawn crashed the game.
-- * Every loop body and hook body runs inside pcall with a circuit breaker (rb/loop.lua).
-- * All Lua of this mod runs on the game thread: LoopInGameThreadWithDelay, never LoopAsync.
--   Only this file's top level runs on UE4SS's own thread, once, at load: it loads the overlay,
--   registers one hook and starts the loops last, so no loop callback runs while it still runs.
--
-- Modules live in Scripts/rb/ and are loaded with loadfile from this script's folder, not with
-- require: whether require finds files of a mod loaded through ModsFolderPaths is untested.
-- A module receives the loader as its chunk argument (`local use = ...`), has no side effects
-- when loaded and returns its table. Only this file starts loops and registers hooks.

-- Paths follow the repository: this file is <repo>/ue4ss/mod/ResearchBridge/Scripts/main.lua.
local TAG = "[ResearchBridge] "
local SCRIPT_PATH = (debug.getinfo(1, "S").source:gsub("^@", ""):gsub("\\", "/"))
local SCRIPTS_DIR = SCRIPT_PATH:match("^(.*/)[^/]+$")
local REPO = SCRIPT_PATH:match("^(.*)/ue4ss/mod/[^/]+/Scripts/[^/]+$")
if not REPO or not SCRIPTS_DIR then
    print(TAG .. "unexpected location " .. SCRIPT_PATH
        .. ", expected <repo>/ue4ss/mod/ResearchBridge/Scripts/main.lua\n")
    return
end

local modules = {}
local function use(name)
    local module = modules[name]
    if module then
        return module
    end
    if module == false then
        error("circular module dependency at rb/" .. name .. ".lua")
    end
    modules[name] = false
    local chunk, loadError = loadfile(SCRIPTS_DIR .. "rb/" .. name .. ".lua")
    if not chunk then
        error("cannot load rb/" .. name .. ".lua: " .. tostring(loadError))
    end
    module = chunk(use)
    modules[name] = module
    return module
end

local protocol, loop, panels, state, selector, commands
local okModules, loadError = pcall(function()
    protocol = use("protocol")
    loop = use("loop")
    panels = use("panels")
    state = use("state")
    selector = use("select")
    commands = use("commands")
end)
if not okModules then
    print(TAG .. tostring(loadError) .. "\n")
    return
end

protocol.init(REPO, TAG)
protocol.rotateLogs()
protocol.removeLeftoverCommand()

if not loop.available() then
    protocol.say("this UE4SS build is too old: LoopInGameThreadWithDelay is missing (needs "
        .. loop.MINIMUM_UE4SS .. " or newer); nothing started")
    return
end

local STATE_PERIOD_MS = 1000
local FAST_PERIOD_MS = 100
local CRACKED_CHANGED = "/Script/Astro.ControlPanel:CrackedChanged"
local SERVER_REQUEST_UNLOCK = "/Script/Astro.AstroPlayerController:ServerRequestUnlockItem"

local function registerHook(functionPath, name, body)
    local guarded = loop.guard(name, body)
    local ok, err = pcall(RegisterHook, functionPath, function(...)
        guarded(...)
    end)
    if ok then
        protocol.logEvent("hooked " .. functionPath)
    else
        protocol.say("cannot hook " .. functionPath .. ": " .. tostring(err))
    end
end

-- Set by the state loop the first time a world is loaded; stays set for the session.
local worldSeen = false

-- The catalog and backpack pollers look their object up with FindFirstOf ten times a second,
-- which walks the whole object array whenever nothing matches, as before the first world. So they
-- start with the first world, the order that was tested in game.
local function afterFirstWorld(task)
    return function()
        if worldSeen then
            task()
        end
    end
end

-- Fast loop: each task has its own breaker, so one broken poller does not stop the others
-- (commands included). It runs from load, so a command sent from the main menu gets
-- not_in_world instead of waiting for a world. An error in the select step ends the running job
-- with call_failed (selector.fail), so a failing job still gets its one result.
local fastTasks = {
    loop.guard("write retry", protocol.flushPending),
    loop.guard("catalog poller", afterFirstWorld(panels.pollCatalog)),
    loop.guard("backpack poller", afterFirstWorld(panels.pollBackpack)),
    loop.guard("printer watch", panels.watchActivePrinter),
    loop.guard("select step", selector.step, selector.fail),
    loop.guard("command poller", commands.poll),
}

local function fastTick()
    for _, task in ipairs(fastTasks) do
        task()
    end
end

local fastLoopStarted = false
local function startFastLoop()
    local started, err = loop.every("fast loop", FAST_PERIOD_MS, fastTick)
    fastLoopStarted = started
    return started, err
end

-- State loop: runs from load and tolerates "no world yet". It also retries a fast loop that could
-- not start. The panel hook is registered once a world is loaded, where control panels exist;
-- that is the order that was tested in game.
local wasInWorld = false
local function stateTick()
    local okRead, s, inWorld = pcall(state.read)
    if not okRead then
        s = { t = os.time(), ok = false, error = tostring(s) }
    end
    protocol.writeFile("state.json", protocol.json(s))
    if not fastLoopStarted then
        local started, err = startFastLoop()
        if not started then
            protocol.logThrottled("cannot start the fast loop: " .. tostring(err))
        end
    end
    if not okRead then
        return
    end
    if wasInWorld and not inWorld then
        selector.forgetGrids()
    end
    wasInWorld = inWorld
    if inWorld and not worldSeen then
        worldSeen = true
        registerHook(CRACKED_CHANGED, "printer panel hook", panels.onCrackedChanged)
    end
end

-- In-game overlay -------------------------------------------------------------------------------
-- Loads a copy of overlay/bin/ResearchOverlay.dll, so the build output stays writable. The copy
-- stays locked while the game runs, so a hot reload (Ctrl+R) does not load a second overlay; the
-- DLL pins itself, so the reload cannot unload it either. New builds load after a game restart.
local function loadOverlay()
    local source = io.open(REPO .. "/overlay/bin/ResearchOverlay.dll", "rb")
    if not source then
        return "overlay DLL not built yet (overlay/bin/ResearchOverlay.dll)"
    end
    local bytes = source:read("a")
    source:close()
    local activePath = protocol.OUT .. "ResearchOverlay-active.dll"
    os.remove(activePath)
    local copy = io.open(activePath, "wb")
    if not copy then
        return "overlay already loaded in this game process"
    end
    local written = copy:write(bytes)
    local closed = copy:close()
    if not written or not closed then
        return "cannot copy the overlay DLL"
    end
    if not (package and package.loadlib) then
        return "package.loadlib is unavailable, overlay not loaded"
    end
    local loaded, err = package.loadlib(activePath, "*")
    if not loaded then
        return "overlay load failed: " .. tostring(err)
    end
    return "overlay loaded"
end

-- Start ------------------------------------------------------------------------------------------
local okOverlay, overlayMessage = pcall(loadOverlay)
if okOverlay then
    protocol.say(overlayMessage)
else
    protocol.say("overlay load failed: " .. tostring(overlayMessage))
end

registerHook(SERVER_REQUEST_UNLOCK, "unlock request hook", commands.logUnlockRequest)

protocol.say("loaded")

-- The loops start last (see the safety rules above).
local stateStarted, stateError = loop.every("state loop", STATE_PERIOD_MS, stateTick)
if not stateStarted then
    protocol.say("cannot start the state loop: " .. tostring(stateError))
end
local fastStarted, fastError = startFastLoop()
if not fastStarted then
    protocol.say("cannot start the fast loop: " .. tostring(fastError))
end
