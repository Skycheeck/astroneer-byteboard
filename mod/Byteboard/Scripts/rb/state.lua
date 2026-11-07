-- Builds the out/state.json object (docs/PROTOCOL.md): research balance, unlocked items and the
-- open printer's recipe.
local use = ...
local protocol = use("protocol")
local uobj = use("uobj")
local panels = use("panels")

local M = {}

local NESTED_COUNTS_DEPTH = 3

local function arrayLength(array)
    if not uobj.valid(array) then
        return nil
    end
    return array:GetArrayNum()
end

-- protocol.json writes an empty table as an array, so an empty object is left out instead.
local function objectOrNil(fields)
    if next(fields) == nil then
        return nil
    end
    return fields
end

-- Raw research fields of AstroGameState or AstroPlayerState, reported for diagnostics.
local function researchFields(obj)
    local fields = {}
    pcall(function()
        fields.balance = obj.CurrentResearchPointBalance
    end)
    pcall(function()
        fields.perMinute = obj.CurrentResearchPointsPerMinute
    end)
    pcall(function()
        fields.knownCount = arrayLength(obj.KnownItemTypes)
    end)
    pcall(function()
        fields.unlockedCount = arrayLength(obj.UnlockedItemTypes)
    end)
    return fields
end

-- Class paths of obj.UnlockedItemTypes. Returns the list and, when it could not be read
-- completely, an error text. An empty slot names no item and is skipped.
local function readUnlocked(obj)
    local array = obj.UnlockedItemTypes
    if not uobj.valid(array) then
        return {}, "UnlockedItemTypes is not readable"
    end
    local expected = array:GetArrayNum()
    local list = {}
    local visited = 0
    local unreadable = 0
    array:ForEach(function(_, element)
        visited = visited + 1
        local itemClass = element:get()
        if not uobj.valid(itemClass) then
            return
        end
        local path = uobj.pathOf(itemClass)
        if path then
            list[#list + 1] = path
        else
            unreadable = unreadable + 1
        end
    end)
    if visited ~= expected then
        return list, "read " .. visited .. " of " .. tostring(expected) .. " entries"
    end
    if unreadable > 0 then
        return list, unreadable .. " entries have no readable class path"
    end
    return list, nil
end

-- GetAvailableIngredientCounts comes back as a TArray, or, depending on the UE4SS build, as a Lua
-- table of parameter wrappers around one; elements are wrappers around numbers. A TArray is a
-- remote object with IsValid and is iterated only when valid; a parameter wrapper has no IsValid
-- and is only unwrapped with get(). A count that cannot be read becomes "?".
local function unwrap(param)
    local ok, inner = pcall(function()
        return param:get()
    end)
    if ok and inner ~= param then
        return inner
    end
    return nil
end

local function appendCounts(out, value, depth)
    if type(value) == "number" then
        out[#out + 1] = tostring(math.tointeger(value) or value)
        return
    end
    if depth >= NESTED_COUNTS_DEPTH or value == nil then
        out[#out + 1] = "?"
        return
    end
    if type(value) == "table" then
        for _, item in ipairs(value) do
            appendCounts(out, item, depth + 1)
        end
        return
    end
    local hasIsValid, isValid = pcall(function()
        return value:IsValid()
    end)
    if not hasIsValid then
        appendCounts(out, unwrap(value), depth + 1)
    elseif isValid == true then
        value:ForEach(function(_, element)
            appendCounts(out, unwrap(element), depth + 1)
        end)
    else
        out[#out + 1] = "?"
    end
end

local function countsText(value)
    if value == nil then
        error("no ingredient counts returned")
    end
    local out = {}
    appendCounts(out, value, 0)
    return table.concat(out, ",")
end

-- The open printer: the recipe it shows (an item's world actor class), the ingredient counts it
-- can reach and whether it can print. Each value is read on its own, so one failure does not hide
-- the others; failures go to events.log.
local function readPrinter(s)
    local printer = panels.activePrinterComponent()
    if not printer then
        return
    end
    local okItem, item = pcall(function()
        return printer:GetCurrentItem()
    end)
    if okItem then
        s.printerCurrent = uobj.pathOf(item)
    end
    local okCan, canPrint = pcall(function()
        return printer:GetCanPrint()
    end)
    if okCan and type(canPrint) == "boolean" then
        s.printerCanPrint = canPrint
    else
        protocol.logThrottled("GetCanPrint failed: " .. tostring(canPrint))
    end
    -- Counts what is in reach of the printer, not what is already inserted.
    local okCounts, counts = pcall(function()
        return countsText(printer:GetAvailableIngredientCounts())
    end)
    if okCounts then
        s.printerHave = counts
    else
        protocol.logThrottled("GetAvailableIngredientCounts failed: " .. tostring(counts))
    end
end

-- The game's display language ("ru", "en", "de", ...), from the static
-- KismetInternationalizationLibrary:GetCurrentLanguage, as the game's language setting sets it. It
-- rarely changes, so it is read again only every LANGUAGE_PERIOD_SECONDS. nil when unreadable.
local LANGUAGE_PERIOD_SECONDS = 10
local language = { value = nil, readAt = nil }

local function currentLanguage(now)
    if language.readAt and now - language.readAt < LANGUAGE_PERIOD_SECONDS then
        return language.value
    end
    language.readAt = now
    local library = uobj.findValid("/Script/Engine.Default__KismetInternationalizationLibrary")
    if not library then
        return language.value
    end
    local ok, value = pcall(function()
        local result = library:GetCurrentLanguage()
        if type(result) == "string" then
            return result
        end
        return result:ToString()
    end)
    if ok and type(value) == "string" and value ~= "" then
        language.value = value
    end
    return language.value
end

-- Returns the state object and whether a world is loaded (game state and local player controller
-- found), which can be true while ok is false.
function M.read()
    local s = { t = os.time(), ok = false }
    s.language = currentLanguage(s.t)
    readPrinter(s)
    local gameState = uobj.gameState()
    local controller = uobj.localPlayerController()
    if not gameState or not controller then
        s.error = "not in a world yet"
        return s, false
    end
    local gameFields = researchFields(gameState)
    s.gameState = objectOrNil(gameFields)
    local playerState = nil
    local playerFields = {}
    local okPlayerState, value = pcall(function()
        return controller.PlayerState
    end)
    if okPlayerState and uobj.valid(value) then
        playerState = value
        playerFields = researchFields(playerState)
        s.playerState = objectOrNil(playerFields)
    end

    -- AstroGameState holds the shared balance and unlock list of the default research mode;
    -- AstroPlayerState has the same fields for per-player research modes. The list comes from the
    -- player state only when the game state's is empty and the player state's is not.
    local source = gameState
    local sourceFields = gameFields
    if playerState and (gameFields.unlockedCount or 0) == 0 and (playerFields.unlockedCount or 0) > 0 then
        source = playerState
        sourceFields = playerFields
    end

    -- What the catalog UI itself shows: static UResearchSystem::GetCurrentResearchPointBalance(PC).
    local research = uobj.researchSystem()
    if research then
        local okBalance, balance = pcall(function()
            return research:GetCurrentResearchPointBalance(controller)
        end)
        if okBalance and type(balance) == "number" then
            s.balance = balance
        end
        local okRate, perMinute = pcall(function()
            return research:GetCurrentResearchPointsPerMinute(controller)
        end)
        if okRate and type(perMinute) == "number" then
            s.perMinute = perMinute
        end
    end
    if type(s.balance) ~= "number" then
        s.balance = sourceFields.balance
    end
    if type(s.perMinute) ~= "number" then
        s.perMinute = sourceFields.perMinute
    end

    local okList, list, listError = pcall(readUnlocked, source)
    if not okList then
        listError = tostring(list)
        list = {}
    end
    s.unlocked = list
    s.unlockedError = listError

    if type(s.balance) ~= "number" then
        s.error = "research balance is not readable"
    elseif listError then
        s.error = "unlocked list is incomplete"
    else
        s.ok = true
    end
    return s, true
end

return M
