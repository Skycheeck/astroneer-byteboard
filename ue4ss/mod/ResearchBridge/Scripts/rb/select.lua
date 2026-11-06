-- The select job: steps the open printer until it shows the requested recipe.
--
-- The printer's recipes form a grid: rows are base items, columns are their variants.
-- Increment/DecrementBlueprint move within the current row, Increment/DecrementBlueprintRow switch
-- rows, and moves are planned as if both wrap around. Every step uses doServerIncrement = true and
-- lands a little later, so the fast loop advances the job one tick at a time (M.step).
--
-- A reading (recipe, row, column, row and column counts) counts as settled when two ticks in a row
-- return the same values, so a half-applied step is never used. The position always comes from
-- what the printer shows, never from counting steps. Each tick the job is in one of three states:
-- * stepping (job.inFlight is set): a step was sent, and the job waits until the printer has left
--   the cell it was on and settled. A step that has not moved the printer after a wait (twice the
--   last landing time, clamped to 0.5-2 s) is sent again, up to 3 times.
-- * holding (job.hold is set): a resent step can land twice, and the second landing can come after
--   the first one has settled. So after a step that needed a resend, the job acts again only once
--   the printer has stayed still for the same wait; any move restarts the wait. Otherwise it could
--   report ok, or plan the next step, while a duplicate is still on the way. Steps can also still
--   be on the way when a job ends, whatever its result (superseded, no_response, timeout...), so
--   the next job on the same printer starts in this state too (grid.pendingHoldTicks).
-- * deciding: on a settled reading the job reports ok when the printer shows the recipe, and
--   otherwise sends the next step.
--
-- Cost: the settle rule makes even a step that lands at once take two ticks (0.2 s): it is sent
-- on one tick, seen moving on the next and seen settled on the one after, which sends the next
-- step. The 20 s job limit (docs/PROTOCOL.md) therefore covers about 100 steps; a full scan of
-- the biggest printer grid seen in game needed about 70. A resend adds its wait and its hold.
--
-- Planning:
-- * Every recipe seen is remembered per printer (by object path) with its cell, so a known recipe
--   is reached directly ("goto"). If the remembered cell is outside the grid, holds another recipe
--   or is not reached within rows + columns steps, the job falls back to a scan.
-- * A scan walks the unseen columns of the current row, then the nearest row with unseen cells,
--   and reports not_found only when every cell of every row has been seen.
-- * IncrementBlueprintRow's direction is learned per printer from unambiguous row moves.
-- The job is bound to the printer it started on, ends after 20 s or 200 sent steps, and writes
-- exactly one result.
-- GetCurrentItem returns the item's world actor class (its ItemType's PickupActor), not the
-- ItemType class, so both names count as a match.
local use = ...
local protocol = use("protocol")
local uobj = use("uobj")
local panels = use("panels")

local M = {}

-- Job time uses os.clock(), which on Windows (MSVC clock()) counts wall time since process start.
local JOB_SECONDS = 20
local STEP_LIMIT = 200
local RESEND_LIMIT = 3
local MIN_WAIT_TICKS = 5
local MAX_WAIT_TICKS = 20
local UNREADABLE = "?"

-- Per printer path:
-- * cells = { [recipe name] = { row, col } }: where each recipe was last seen;
-- * rowIncrementUp: nil until learned, true when IncrementBlueprintRow raises the row index;
-- * landingTicks: how many ticks the last step took to move the printer;
-- * pendingHoldTicks: the hold the next job must start with, because a job that ended may still
--   have steps on the way (0 when none; set by finish, taken by M.start).
local grids = {}
local job = nil

local function gridOf(printerPath)
    local grid = grids[printerPath]
    if not grid then
        grid = { cells = {}, rowIncrementUp = nil, landingTicks = 1, pendingHoldTicks = 0 }
        grids[printerPath] = grid
    end
    return grid
end

-- How long a step may take to move the printer before it is resent, and how long the printer must
-- stay still in a hold: twice the last landing time, clamped.
local function waitTicks(grid)
    return math.min(MAX_WAIT_TICKS, math.max(MIN_WAIT_TICKS, 2 * grid.landingTicks))
end

local function newHold(ticks)
    return { needed = ticks, quietTicks = 0 }
end

-- How long the next job on j's printer must hold when j ends. A step still on the way may land as
-- late as it has already been waiting (a printer that stalled through all resends, for example),
-- so the hold lasts that long: at least the step's wait, at most MAX_WAIT_TICKS. A hold j was in
-- carries over.
local function holdTicksAfter(j)
    local step = j.inFlight
    if step then
        return math.min(MAX_WAIT_TICKS, math.max(step.waitLimit, step.ticksSinceFirstSend))
    end
    if j.hold then
        return j.hold.needed
    end
    return 0
end

-- Object paths of printers repeat between worlds, so the memory is dropped when the world goes.
function M.forgetGrids()
    grids = {}
end

function M.busy()
    return job ~= nil
end

-- Names a printer can show for an item type class path: the class name and the name of its
-- PickupActor class. nil when the class is not loaded.
function M.recipeNames(classPath)
    if not uobj.findValid(classPath) then
        return nil
    end
    local packagePath, className = classPath:match("^(.*)%.([^%.]+)$")
    if not className then
        return nil
    end
    local names = { [className] = true }
    local defaults = uobj.findValid(packagePath .. ".Default__" .. className)
    if defaults then
        local ok, pickup = pcall(function()
            return defaults.PickupActor
        end)
        if ok then
            local pickupName = uobj.fname(pickup)
            if pickupName ~= UNREADABLE then
                names[pickupName] = true
            end
        end
    end
    return names
end

local function finish(code, detail)
    local j = job
    if not j then
        return
    end
    job = nil
    -- Steps this job sent may still land after it ends; the next job on this printer waits for
    -- them instead of reporting ok and then drifting.
    gridOf(j.printer).pendingHoldTicks = holdTicksAfter(j)
    local summary = string.format("%s after %d steps in %.1f s (%s)", code, j.steps, os.clock() - j.startedAt, j.mode)
    if detail then
        summary = summary .. ": " .. detail
    end
    protocol.logEvent("select " .. j.classPath .. ": " .. summary)
    protocol.logCommand(j.line .. " -> " .. summary)
    local ok = code == "ok"
    local result = { id = j.id, op = "select", arg = j.classPath, ok = ok, code = code }
    if not ok then
        result.error = detail or code
    end
    protocol.writeResult(result)
end

-- Ends the running job after M.step raised an error (the fast loop's onError), so a failing job
-- still writes exactly one result and the next tick finds no job.
function M.fail(message)
    finish("call_failed", message)
end

-- Position ---------------------------------------------------------------------------------------
local function readPosition(printer)
    local item = printer:GetCurrentItem()
    local row = printer:GetCurrentVisibleRowBlueprintIndex()
    local col = printer:GetCurrentVisibleColumnBlueprintIndex()
    local rows = printer:GetTotalVisibleBlueprintRowsCount()
    local cols = printer:GetTotalVisibleBlueprintColumnCount()
    if type(row) ~= "number" or type(col) ~= "number" or type(rows) ~= "number" or type(cols) ~= "number" then
        error("printer position is not readable")
    end
    return { name = uobj.fname(item), row = row, col = col, rows = math.max(1, rows), cols = math.max(1, cols) }
end

local function sameCell(a, b)
    return a ~= nil and b ~= nil and a.name == b.name and a.row == b.row and a.col == b.col
end

local function sameReading(a, b)
    return sameCell(a, b) and a.rows == b.rows and a.cols == b.cols
end

local function rememberedCell(grid, names)
    for name in pairs(names) do
        local cell = grid.cells[name]
        if cell then
            return { row = cell.row, col = cell.col }
        end
    end
    return nil
end

-- Records a settled reading: the cell counts as seen in this job and its recipe is remembered.
local function observe(j, grid, reading)
    local seenInRow = j.seen[reading.row]
    if not seenInRow then
        seenInRow = {}
        j.seen[reading.row] = seenInRow
    end
    if not seenInRow[reading.col] and reading.name == UNREADABLE then
        j.unreadable = j.unreadable + 1
    end
    seenInRow[reading.col] = true
    j.colCount[reading.row] = reading.cols
    if reading.name == UNREADABLE then
        return
    end
    for name, cell in pairs(grid.cells) do
        if cell.row == reading.row and cell.col == reading.col and name ~= reading.name then
            grid.cells[name] = nil
        end
    end
    grid.cells[reading.name] = { row = reading.row, col = reading.col }
end

local function rowComplete(j, row)
    local count = j.colCount[row]
    local seenInRow = j.seen[row]
    if not count or not seenInRow then
        return false
    end
    for col = 0, count - 1 do
        if not seenInRow[col] then
            return false
        end
    end
    return true
end

-- Planning ---------------------------------------------------------------------------------------
-- The index nearest to `from` on a ring of n positions for which isDone is false; forward wins ties.
local function nearestOpen(from, n, isDone)
    for distance = 1, n - 1 do
        local forward = (from + distance) % n
        if not isDone(forward) then
            return forward
        end
        local backward = (from - distance) % n
        if not isDone(backward) then
            return backward
        end
    end
    return nil
end

local function colStepToward(reading, targetCol)
    local forward = (targetCol - reading.col) % reading.cols
    if forward <= reading.cols - forward then
        return "col+"
    end
    return "col-"
end

local function rowStepToward(grid, reading, targetRow)
    local forward = (targetRow - reading.row) % reading.rows
    local wantHigherIndex = forward <= reading.rows - forward
    local incrementGoesUp = grid.rowIncrementUp ~= false
    if wantHigherIndex == incrementGoesUp then
        return "row+"
    end
    return "row-"
end

local function switchToScan(j, grid, reason)
    for name in pairs(j.names) do
        grid.cells[name] = nil
    end
    j.mode = "scan"
    protocol.logEvent("select " .. j.classPath .. ": " .. reason .. ", scanning")
    return nil
end

local function gotoStep(j, grid, reading)
    local goal = j.goal
    if goal.row >= reading.rows then
        return switchToScan(j, grid, "remembered row is outside the grid")
    end
    if j.gotoSteps >= reading.rows + reading.cols then
        return switchToScan(j, grid, "remembered cell not reached")
    end
    if reading.row ~= goal.row then
        j.gotoSteps = j.gotoSteps + 1
        return rowStepToward(grid, reading, goal.row)
    end
    if goal.col >= reading.cols then
        return switchToScan(j, grid, "remembered column is outside the row")
    end
    if reading.col ~= goal.col then
        j.gotoSteps = j.gotoSteps + 1
        return colStepToward(reading, goal.col)
    end
    -- The remembered cell holds another recipe now: the grid changed, so forget all of it.
    grid.cells = {}
    return switchToScan(j, grid, "remembered cell holds another recipe")
end

local function scanStep(j, grid, reading)
    local seenInRow = j.seen[reading.row] or {}
    local col = nearestOpen(reading.col, reading.cols, function(c)
        return seenInRow[c] == true
    end)
    if col then
        return colStepToward(reading, col)
    end
    local row = nearestOpen(reading.row, reading.rows, function(r)
        return rowComplete(j, r)
    end)
    if row then
        return rowStepToward(grid, reading, row)
    end
    return nil
end

-- The next step to send, or nil when every cell has been seen.
local function nextStep(j, grid, reading)
    if j.mode == "scan" then
        -- The target was seen in this job, but the printer is no longer on it.
        local cell = rememberedCell(grid, j.names)
        if cell then
            j.mode = "goto"
            j.goal = cell
            j.gotoSteps = 0
        end
    end
    if j.mode == "goto" then
        local kind = gotoStep(j, grid, reading)
        if kind then
            return kind
        end
    end
    return scanStep(j, grid, reading)
end

-- Steps ------------------------------------------------------------------------------------------
local function send(j, printer, kind)
    if kind == "col+" then
        printer:IncrementBlueprint(true)
    elseif kind == "col-" then
        printer:DecrementBlueprint(true)
    elseif kind == "row+" then
        printer:IncrementBlueprintRow(true)
    else
        printer:DecrementBlueprintRow(true)
    end
    j.steps = j.steps + 1
end

-- Sends a new step; `from` is the settled reading of the cell the printer leaves.
local function startStep(j, grid, printer, kind, from)
    j.inFlight = {
        kind = kind,
        from = from,
        resends = 0,
        ticksSinceSend = 0,       -- since the latest send or resend, compared with waitLimit
        ticksSinceFirstSend = 0,
        ticksToMove = nil,        -- ticks from the first send until the printer left `from`
        waitLimit = waitTicks(grid),
    }
    send(j, printer, kind)
end

-- Learns which way IncrementBlueprintRow moves. Only a move by exactly one row on a grid of more
-- than two rows is unambiguous.
local function learnRowDirection(grid, kind, before, after)
    local n = after.rows
    if n <= 2 or before.rows ~= n then
        return
    end
    local moved = (after.row - before.row) % n
    if moved ~= 1 and moved ~= n - 1 then
        return
    end
    local up = moved == 1
    if kind == "row-" then
        up = not up
    end
    grid.rowIncrementUp = up
end

-- Follows the step on the way. Returns true once it has landed: the printer left the cell the step
-- started from and the reading settled. May resend the step or end the job (no_response).
local function followStep(j, grid, printer, reading, settled)
    local step = j.inFlight
    step.ticksSinceSend = step.ticksSinceSend + 1
    step.ticksSinceFirstSend = step.ticksSinceFirstSend + 1
    if sameCell(reading, step.from) then
        if step.ticksSinceSend < step.waitLimit then
            return false
        end
        if step.resends >= RESEND_LIMIT then
            finish("no_response", "the printer did not move after " .. RESEND_LIMIT .. " resends")
            return false
        end
        step.resends = step.resends + 1
        step.ticksSinceSend = 0
        send(j, printer, step.kind)
        return false
    end
    if not step.ticksToMove then
        step.ticksToMove = step.ticksSinceFirstSend
    end
    if not settled then
        return false
    end
    j.inFlight = nil
    grid.landingTicks = step.ticksToMove
    if step.resends > 0 then
        -- A duplicate may still land, and a move it causes must not teach the row direction.
        j.hold = newHold(waitTicks(grid))
    elseif step.kind == "row+" or step.kind == "row-" then
        learnRowDirection(grid, step.kind, step.from, reading)
    end
    return true
end

-- Returns true once the printer has stayed still for the hold's ticks; a move restarts the count.
local function holdOver(hold, settled)
    if not settled then
        hold.quietTicks = 0
        return false
    end
    hold.quietTicks = hold.quietTicks + 1
    return hold.quietTicks >= hold.needed
end

-- Advances the job by one tick (fast loop). See the states at the top of this file.
function M.step()
    local j = job
    if not j then
        return
    end
    local activePath = panels.activePrinterPath()
    if activePath ~= j.printer then
        if activePath then
            return finish("printer_changed", "another printer was opened")
        end
        return finish("printer_closed", "the printer panel closed")
    end
    local printer = uobj.findValid(j.printer)
    if not printer then
        return finish("printer_closed", "the printer is gone")
    end
    if os.clock() > j.deadline then
        return finish("timeout", "no result within " .. JOB_SECONDS .. " s")
    end

    local grid = gridOf(j.printer)
    local reading = readPosition(printer)
    local settled = sameReading(reading, j.lastReading)
    j.lastReading = reading
    if settled then
        observe(j, grid, reading)
    end

    if j.inFlight and not followStep(j, grid, printer, reading, settled) then
        return
    end
    if j.hold then
        if not holdOver(j.hold, settled) then
            return
        end
        j.hold = nil
    elseif not settled then
        return
    end

    if reading.name ~= UNREADABLE and j.names[reading.name] then
        return finish("ok")
    end
    if j.steps >= STEP_LIMIT then
        return finish("timeout", "gave up after " .. STEP_LIMIT .. " steps, printer shows " .. reading.name)
    end
    local kind = nextStep(j, grid, reading)
    if not kind then
        local detail = "every cell of " .. reading.rows .. " rows was seen"
        if j.unreadable > 0 then
            detail = detail .. ", " .. j.unreadable .. " of them unreadable"
        end
        return finish("not_found", detail)
    end
    startStep(j, grid, printer, kind, reading)
end

-- Starts a job for cmd (its arg is the class path) on the printer at printerPath. A running job
-- ends as superseded. When an earlier job on this printer may still have steps on the way
-- (finish), the new job starts in a hold. The new job is installed last, so an error before that
-- leaves no job behind whose result would follow the caller's call_failed.
function M.start(cmd, printerPath, names)
    finish("superseded", "a newer select replaced this one")
    local grid = gridOf(printerPath)
    local goal = rememberedCell(grid, names)
    if goal then
        protocol.logEvent("select " .. cmd.arg .. ": remembered row " .. goal.row .. " col " .. goal.col)
    else
        protocol.logEvent("select " .. cmd.arg .. ": unknown, scanning")
    end
    local inheritedHold = nil
    if grid.pendingHoldTicks > 0 then
        inheritedHold = newHold(grid.pendingHoldTicks)
    end
    local now = os.clock()
    grid.pendingHoldTicks = 0
    job = {
        -- The command.
        id = cmd.id,
        line = cmd.line,
        classPath = cmd.arg,
        names = names,              -- recipe names that count as a match (M.recipeNames)
        printer = printerPath,      -- object path of the printer the job is bound to
        startedAt = now,
        deadline = now + JOB_SECONDS,
        -- Planning.
        mode = goal and "goto" or "scan",
        goal = goal,                -- { row, col } that goto mode heads for
        gotoSteps = 0,              -- steps planned in goto mode, capped at rows + columns
        seen = {},                  -- seen[row][col] is true for every cell read in this job
        colCount = {},              -- colCount[row] is the column count read in that row
        unreadable = 0,             -- seen cells whose recipe name could not be read
        -- Stepping.
        steps = 0,                  -- steps sent, resends included
        lastReading = nil,          -- the previous tick's reading, for the settle rule
        inFlight = nil,             -- the step on the way (startStep)
        hold = inheritedHold,       -- a quiet wait before the next decision (newHold)
    }
end

return M
