-- Which research panel or printer is open, signalled to the overlay through out/catalog.txt and
-- out/printer.txt, and the printer the select, print and state code work on.
--
-- Single-player only. On a multiplayer host the panel hook also fires for other players' printer
-- panels, and FindFirstOf can return another player's catalog or backpack, so another player's
-- printer could open the overlay and receive select and print. Nothing here checks that a panel
-- belongs to the local player.
local use = ...
local protocol = use("protocol")
local uobj = use("uobj")

local M = {}

local CATALOG_SIGNAL = "catalog.txt"
local PRINTER_SIGNAL = "printer.txt"
local BACKPACK_CATEGORY = "ItemCategory_Backpack"
local CATALOG_DEBOUNCE_TICKS = 3

-- The printer whose panel (or backpack) is open: only its object path is kept.
local active = { path = nil, isBackpack = false }

function M.activePrinterPath()
    return active.path
end

function M.activePrinterComponent()
    if not active.path then
        return nil
    end
    return uobj.findValid(active.path)
end

local function openPrinter(path, isBackpack, category)
    active.path = path
    active.isBackpack = isBackpack
    protocol.writeSignal(PRINTER_SIGNAL, "open " .. category)
end

local function closePrinter()
    active.path = nil
    active.isBackpack = false
    protocol.writeSignal(PRINTER_SIGNAL, "closed")
end

-- Research catalog: BackpackCatalog_C.ExtendTop is false while the catalog is open (checked in
-- game). A new state counts only after it held for 3 ticks (0.3 s), so a flag that flips briefly
-- while the backpack moves does not open and close the overlay. A catalog that disappears (world
-- unload) counts as closed.
local catalog = { open = nil, pending = nil, pendingTicks = 0 }

function M.pollCatalog()
    local widget = uobj.findFirst("BackpackCatalog_C")
    local open = widget ~= nil and widget.ExtendTop == false
    if catalog.open == nil then
        catalog.open = open
        return
    end
    if open == catalog.open then
        catalog.pending = nil
        return
    end
    if open ~= catalog.pending then
        catalog.pending = open
        catalog.pendingTicks = 0
    end
    catalog.pendingTicks = catalog.pendingTicks + 1
    if catalog.pendingTicks < CATALOG_DEBOUNCE_TICKS then
        return
    end
    catalog.open = open
    catalog.pending = nil
    protocol.writeSignal(CATALOG_SIGNAL, open and "open" or "closed")
end

-- Printer panels: opening a ControlPanel "cracks" it, and ControlPanel:CrackedChanged(component,
-- bIsCracked) reports both directions. The printer is the panel's ControlledActor (or its owner,
-- or the actor it is attached to); DefaultBlueprintPrinterCatagory names what it prints
-- (ItemCategory_SmFab, ...).
local function printerOfPanel(panel)
    local actorGetters = {
        function()
            return panel.ControlledActor
        end,
        function()
            return panel:GetOwner()
        end,
        function()
            return panel:GetAttachParentActor()
        end,
    }
    for _, getActor in ipairs(actorGetters) do
        local ok, actor = pcall(getActor)
        if ok then
            local component = uobj.printerComponentOf(actor)
            if component then
                return component
            end
        end
    end
    return nil
end

-- Hook body for ControlPanel:CrackedChanged(component, bIsCracked).
function M.onCrackedChanged(selfParam, _, crackedParam)
    local panel = selfParam:get()
    if not uobj.valid(panel) then
        return  -- also fires for panels being torn down
    end
    local component = printerOfPanel(panel)
    if not component then
        return
    end
    local category = "?"
    local okCategory, categoryObject = pcall(function()
        return component.DefaultBlueprintPrinterCatagory
    end)
    if okCategory then
        category = uobj.fname(categoryObject)
    end
    if not category:find("^ItemCategory_") then
        return  -- e.g. the backpack printer, which has no category
    end
    local path = uobj.pathOf(component)
    if not path then
        return
    end
    local okCracked, crackedValue = pcall(function()
        return crackedParam:get()
    end)
    local cracked = okCracked and crackedValue == true
    protocol.logEvent("printer panel " .. uobj.describe(panel) .. " cracked=" .. tostring(cracked) .. " " .. category)
    if cracked then
        openPrinter(path, false, category)
    elseif active.path == path then
        closePrinter()  -- only the open printer's own panel closes it
    end
end

-- Backpack (Q): BackpackRail_C.IsRaised is true while the backpack is open. Its printer is the
-- backpack's PrinterComponent, which has no category, so the overlay is told ItemCategory_Backpack.
-- A rail that disappears (world unload) counts as a closed backpack.
local backpackOpen = nil

function M.pollBackpack()
    local rail = uobj.findFirst("BackpackRail_C")
    local open = rail ~= nil and rail.IsRaised == true
    if open == backpackOpen then
        return
    end
    local first = backpackOpen == nil
    backpackOpen = open
    if first then
        return
    end
    if not open then
        if active.isBackpack then
            closePrinter()
        end
        return
    end
    local path = uobj.pathOf(uobj.printerComponentOf(rail))
    if not path then
        protocol.logEvent("backpack opened, but it has no printer component")
        return
    end
    openPrinter(path, true, BACKPACK_CATEGORY)
end

-- A printer or backpack that disappears while open (world unload, return to the main menu,
-- printer picked up) sends no close event, so the open one is looked up every tick.
function M.watchActivePrinter()
    if not active.path or uobj.findValid(active.path) then
        return
    end
    protocol.logEvent("open printer is gone: " .. active.path)
    closePrinter()
end

return M
