-- Runs the command in in/cmd.txt (docs/PROTOCOL.md): unlock, select or print.
-- Commands come from the in-game overlay through a local file. Only unlock
-- spends bytes, and it runs only while in/allow-unlock exists; select and print do what the
-- printer's own buttons do. Every result carries a machine-readable code.
local use = ...
local protocol = use("protocol")
local uobj = use("uobj")
local panels = use("panels")
local selector = use("select")

local M = {}

local COMMAND_MAX_AGE_SECONDS = 8
local ITEM_CLASS_PATH = "^/Game/Items/ItemTypes/[%w_/%-]+%.[%w_%-]+_C$"
-- Items that exist only in Creative Mode; the game hides them in code, but ServerRequestUnlockItem
-- would still unlock them. The extraction tools keep their own copy of this list.
local CREATIVE_ONLY = { CreativeDroneIT_C = true }  -- Aero, the camera drone
-- Returned by a handler whose result is written later (select).
local DEFERRED = {}

local function refuse(code, message)
    return { ok = false, code = code, error = message }
end

local function done()
    return { ok = true, code = "ok" }
end

local function unlock(cmd, controller)
    local className = cmd.arg:match("%.([%w_%-]+)$")
    if CREATIVE_ONLY[className] then
        return refuse("creative_only", "the item exists only in Creative Mode")
    end
    if not protocol.unlockAllowed() then
        return refuse("disabled", "unlock disabled (no in/allow-unlock)")
    end
    local itemClass = uobj.findValid(cmd.arg)
    if not itemClass then
        return refuse("class_not_loaded", "class not loaded: " .. cmd.arg)
    end
    local research = uobj.researchSystem()
    if not research then
        return refuse("call_failed", "ResearchSystem not found")
    end
    local checked, alreadyUnlocked = pcall(function()
        return research:IsItemTypeUnlocked(controller, itemClass)
    end)
    if not checked then
        protocol.logThrottled("IsItemTypeUnlocked failed: " .. tostring(alreadyUnlocked))
    elseif alreadyUnlocked == true then
        return refuse("already_unlocked", "already unlocked")
    end
    controller:ServerRequestUnlockItem(itemClass, false)
    return done()
end

local function selectRecipe(cmd)
    local printerPath = panels.activePrinterPath()
    if not printerPath or not uobj.findValid(printerPath) then
        return refuse("no_printer", "no printer panel open")
    end
    local names = selector.recipeNames(cmd.arg)
    if not names then
        return refuse("class_not_loaded", "class not loaded: " .. cmd.arg)
    end
    selector.start(cmd, printerPath, names)
    return DEFERRED
end

-- arg, when given, names the recipe the sender expects on the printer; while a select job steps
-- the printer, the recipe shown is an intermediate one, so print waits for the job to end.
local function printRecipe(cmd)
    if selector.busy() then
        return refuse("busy", "a recipe selection is still running")
    end
    local printer = panels.activePrinterComponent()
    if not printer then
        return refuse("no_printer", "no printer panel open")
    end
    if cmd.arg then
        local names = selector.recipeNames(cmd.arg)
        if not names then
            return refuse("class_not_loaded", "class not loaded: " .. cmd.arg)
        end
        local shown = uobj.fname(printer:GetCurrentItem())
        if not names[shown] then
            return refuse("wrong_recipe", "the printer shows " .. shown)
        end
    end
    if printer:GetCanPrint() ~= true then
        return refuse("cannot_print", "the printer cannot print now (resources or power)")
    end
    printer:StartPrinting()
    return done()
end

local handlers = { unlock = unlock, select = selectRecipe, print = printRecipe }

local function run(cmd)
    if cmd.parseError then
        return refuse("bad_command", cmd.parseError)
    end
    local handler = handlers[cmd.op]
    if not handler then
        return refuse("unknown_op", "unknown op: " .. cmd.op)
    end
    if cmd.arg and not cmd.arg:match(ITEM_CLASS_PATH) then
        return refuse("bad_command", "not an item type class path: " .. cmd.arg)
    end
    if not cmd.arg and cmd.op ~= "print" then
        return refuse("bad_command", cmd.op .. " needs an item type class path")
    end
    local age = os.time() - cmd.unixSec
    if age > COMMAND_MAX_AGE_SECONDS then
        return refuse("expired", "the command is " .. age .. " s old")
    end
    -- The same test as state.json's world check, so a command never runs in the main menu.
    local controller = uobj.localPlayerController()
    if not controller or not uobj.gameState() then
        return refuse("not_in_world", "not in a world yet")
    end
    local ok, result = pcall(handler, cmd, controller)
    if not ok then
        return refuse("call_failed", tostring(result))
    end
    return result
end

-- Fast loop: runs the pending command, if any. cmd.txt is already deleted when run starts, so an
-- error there still writes a result; otherwise the command would get none.
function M.poll()
    local cmd = protocol.takeCommand()
    if not cmd then
        return
    end
    local ran, result = pcall(run, cmd)
    if not ran then
        result = refuse("call_failed", tostring(result))
    end
    if result == DEFERRED then
        protocol.logCommand(cmd.line .. " -> started")
        return
    end
    result.id = cmd.id
    result.op = cmd.op
    result.arg = cmd.arg or ""
    if result.ok then
        protocol.logCommand(cmd.line .. " -> ok")
    else
        protocol.logCommand(cmd.line .. " -> " .. result.code .. ": " .. tostring(result.error))
    end
    protocol.writeResult(result)
end

-- Hook body for AstroPlayerController:ServerRequestUnlockItem: records every unlock request,
-- including the ones the game's own catalog sends.
function M.logUnlockRequest(_, itemParam, allowPlayFabParam)
    local name = "?"
    local okItem, item = pcall(function()
        return itemParam:get()
    end)
    if okItem then
        name = uobj.pathOf(item) or "?"
    end
    local flag = "?"
    local okFlag, value = pcall(function()
        return allowPlayFabParam:get()
    end)
    if okFlag then
        flag = tostring(value)
    end
    protocol.logCommand("ServerRequestUnlockItem(" .. name .. ", bAllowPlayFabBinding=" .. flag .. ")")
end

return M
