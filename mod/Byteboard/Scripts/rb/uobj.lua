-- Safe access to game objects. Every other module reaches UObjects through these helpers.
--
-- Rules (each one cost a game crash before):
-- * valid() comes before any method call or property read on a UObject or on an object the game
--   returned. A call on a null or stale object crashes inside UE4SS.dll, and pcall cannot catch
--   that. An object property that holds null still comes back as a wrapper, so a nil check is not
--   enough.
-- * No UObject is kept between ticks: callers store object paths and look the object up again.
-- * Only named properties are read; walking all properties of the player pawn crashed the game.
local M = {}

-- True only for a live object. Wrappers without IsValid (hook parameters, plain Lua values)
-- count as not valid.
function M.valid(o)
    if o == nil then
        return false
    end
    local ok, isValid = pcall(function()
        return o:IsValid()
    end)
    return ok and isValid == true
end

-- Object name ("Splitter_3Way_C"), or "?" when the object is not valid or has no readable name.
function M.fname(o)
    if not M.valid(o) then
        return "?"
    end
    local ok, name = pcall(function()
        return o:GetFName():ToString()
    end)
    if ok and type(name) == "string" then
        return name
    end
    return "?"
end

-- Object path without the class prefix: "BlueprintGeneratedClass /Game/X.X_C" -> "/Game/X.X_C".
-- nil when the object is not valid.
function M.pathOf(o)
    if not M.valid(o) then
        return nil
    end
    local ok, fullName = pcall(function()
        return o:GetFullName()
    end)
    if not ok or type(fullName) ~= "string" then
        return nil
    end
    return (fullName:gsub("^%S+%s+", ""))
end

-- "ClassName:ObjectName" for logs.
function M.describe(o)
    if not M.valid(o) then
        return "nil"
    end
    local ok, text = pcall(function()
        local objectName = o:GetFullName():match("[^%.:]+$") or "?"
        return M.fname(o:GetClass()) .. ":" .. objectName
    end)
    if ok then
        return text
    end
    return "?"
end

function M.findValid(path)
    local ok, o = pcall(StaticFindObject, path)
    if ok and M.valid(o) then
        return o
    end
    return nil
end

-- The first live instance of a class (short name, "BackpackRail_C"), or nil. Walks the object
-- array. An error counts as "none", so a poller does not trip its circuit breaker while a world
-- loads or unloads.
function M.findFirst(className)
    local ok, o = pcall(FindFirstOf, className)
    if ok and M.valid(o) then
        return o
    end
    return nil
end

-- The world's AstroGameState, or nil outside a world.
function M.gameState()
    return M.findFirst("AstroGameState")
end

function M.researchSystem()
    return M.findValid("/Script/Astro.Default__ResearchSystem")
end

-- PrinterComponent's static ActorPrinterComponent(Actor), called on the class default object.
function M.printerComponentOf(actor)
    local defaults = M.findValid("/Script/Astro.Default__PrinterComponent")
    if not defaults or not M.valid(actor) then
        return nil
    end
    local ok, component = pcall(function()
        return defaults:ActorPrinterComponent(actor)
    end)
    if ok and M.valid(component) then
        return component
    end
    return nil
end

-- The local player's controller. A listen-server host has one AstroPlayerController per player,
-- so the first one found is not necessarily ours; IsLocalController tells them apart. If that
-- call fails, a lone controller is still safe to use. Walks the object array, so it is called
-- once per state tick or command, not every fast tick.
function M.localPlayerController()
    local found, controllers = pcall(FindAllOf, "AstroPlayerController")
    if not found or type(controllers) ~= "table" then
        return nil
    end
    local validCount = 0
    local unchecked = nil
    for _, controller in ipairs(controllers) do
        if M.valid(controller) then
            validCount = validCount + 1
            local ok, isLocal = pcall(function()
                return controller:IsLocalController()
            end)
            if ok and isLocal == true then
                return controller
            end
            if not ok then
                unchecked = controller
            end
        end
    end
    if validCount == 1 then
        return unchecked
    end
    return nil
end

return M
