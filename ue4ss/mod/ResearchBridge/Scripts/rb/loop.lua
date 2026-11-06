-- Game-thread loops and circuit breakers.
--
-- All Lua of this mod runs on the game thread: loops use LoopInGameThreadWithDelay, and hook
-- callbacks already run there. LoopAsync runs its callback on UE4SS's async thread in the same Lua
-- state the game-thread callbacks use, and each ExecuteInGameThread call from there adds a registry
-- reference while the game thread reads the registry: two game crashes inside UE4SS's Lua table
-- code (lua_rawgeti on a corrupted table) came from that. There is deliberately no fallback to it.
local use = ...
local protocol = use("protocol")

local M = {}

local FAILURE_LIMIT = 50

M.MINIMUM_UE4SS = "RE-UE4SS experimental 3.0.1-1183-gcdbc0b66"

function M.available()
    return type(LoopInGameThreadWithDelay) == "function" and type(CancelDelayedAction) == "function"
end

-- Wraps fn in pcall. Errors are logged (throttled) and passed to onError; after FAILURE_LIMIT
-- failures in a row the wrapper turns into a no-op and calls onStop once, so a broken game state
-- does not raise errors ten times a second forever. Returns nothing, so it is safe as a hook
-- callback (a returned value could replace the hooked function's return value).
function M.guard(name, fn, onError, onStop)
    local failures = 0
    local stopped = false
    return function(...)
        if stopped then
            return
        end
        local ok, err = pcall(fn, ...)
        if ok then
            failures = 0
            return
        end
        failures = failures + 1
        local message = tostring(err)
        protocol.logThrottled(name .. " error: " .. message)
        if onError then
            pcall(onError, message)
        end
        if failures >= FAILURE_LIMIT then
            stopped = true
            protocol.say(name .. " stopped after " .. FAILURE_LIMIT .. " failures in a row")
            if onStop then
                pcall(onStop)
            end
        end
    end
end

-- Runs fn every ms milliseconds on the game thread, guarded as above; a stopped loop cancels its
-- delayed action. Returns true, or false and the error when the loop could not start.
function M.every(name, ms, fn)
    local handle = nil
    local stopped = false
    local run = M.guard(name, fn, nil, function()
        stopped = true
        if handle ~= nil then
            CancelDelayedAction(handle)
        end
    end)
    local started, result = pcall(LoopInGameThreadWithDelay, ms, function()
        if not stopped then
            pcall(run)
        end
    end)
    if not started then
        return false, tostring(result)
    end
    handle = result
    return true
end

return M
