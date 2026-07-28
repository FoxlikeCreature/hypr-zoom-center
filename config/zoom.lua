-- Hold-to-zoom for Hyprland, anchored to the centre of the monitor.
--
-- Loaded into Hyprland's Lua VM on demand via `zoom on`, torn down by `zoom off`.
-- Nothing here runs unless the mode is explicitly enabled, and no bind of ours
-- exists while it is off.
--
-- The whole point of living inside the VM: the wheel handler runs synchronously
-- during input handling, so a change lands in the very next frame. Shelling out
-- to `hyprctl` per wheel tick would cost a process spawn and a scheduler hop.
--
-- Centring comes from the zoomcenter plugin (~/projects/hypr-zoom-center), which
-- pins the zoom anchor to the middle of the monitor. Hyprland on its own always
-- anchors on the pointer, and moving the pointer there first does not work under
-- a game holding a pointer lock: the compositor puts the cursor straight back.

local cfg = {
    button = "mouse:276", -- BTN_EXTRA. The other side button is mouse:275.
    factor = 3.0,         -- zoom on press; wheel adjustments never persist past release
    step   = 1.15,        -- multiplier per wheel notch
    min    = 1.0,
    max    = 10.0,

    -- Motion is divided by factor^sens_exponent while zoomed. 1.0 keeps the
    -- on-screen speed exactly as it was, 0.0 disables compensation, values in
    -- between soften it.
    sens_exponent = 1.0,

    -- With the anchor pinned these only complicate the render path, so both stay
    -- off: the zoom is already fixed and centred.
    rigid           = false,
    detached_camera = false,
    disable_aa      = false, -- crisper but blockier upscale
}

local WHEEL_UP, WHEEL_DOWN = "mouse_up", "mouse_down"

local plugin = hl.plugin and hl.plugin.zoomcenter

if not plugin then
    error("zoomcenter plugin not loaded — run `hyprpm enable zoomcenter`")
end

local function set(opts)
    hl.config({ cursor = opts })
end

-- A key can carry both a press and a release bind, so unbind until it stops
-- removing anything. Cheap, and it makes teardown idempotent.
local function unbind_all(key)
    for _ = 1, 4 do
        pcall(hl.unbind, key)
    end
end

-- Tear down a previous load before installing over it, so repeated `zoom on`
-- can't stack duplicate binds.
if HYPRZOOM and HYPRZOOM.teardown then
    pcall(HYPRZOOM.teardown)
end

HYPRZOOM = {
    held   = false,
    factor = 1.0,
    -- Remembered so `zoom off` restores whatever was in effect beforehand.
    saved  = {
        zoom_rigid           = hl.get_config("cursor:zoom_rigid"),
        zoom_detached_camera = hl.get_config("cursor:zoom_detached_camera"),
        zoom_disable_aa      = hl.get_config("cursor:zoom_disable_aa"),
    },
}

local function apply(f)
    if f < cfg.min then f = cfg.min end
    if f > cfg.max then f = cfg.max end
    HYPRZOOM.factor = f
    set({ zoom_factor = f })
end

local function wheel(dir)
    return function()
        if not HYPRZOOM.held then return end
        apply(dir > 0 and HYPRZOOM.factor * cfg.step or HYPRZOOM.factor / cfg.step)
    end
end

local function press()
    HYPRZOOM.held = true
    -- Pin before raising the factor: the anchor is read when the factor changes.
    pcall(plugin.pin)
    apply(cfg.factor)
    -- Wheel binds exist only while the button is down. If they lived for the
    -- whole mode they would swallow every scroll: no page scrolling, no weapon
    -- switching. This is correctness, not micro-optimisation.
    hl.bind(WHEEL_UP, wheel(1), { description = "hyprzoom:wheel", ignore_mods = true })
    hl.bind(WHEEL_DOWN, wheel(-1), { description = "hyprzoom:wheel", ignore_mods = true })
end

local function release()
    HYPRZOOM.held = false
    unbind_all(WHEEL_UP)
    unbind_all(WHEEL_DOWN)
    apply(1.0) -- wheel adjustments are deliberately not carried over
    -- Deliberately no unpin here. Zooming out is animated, and the frames still
    -- in flight would re-read the anchor: released early, the view slides off to
    -- the real cursor on the way out. A pinned anchor costs nothing at factor
    -- 1.0 (the transform is skipped entirely), the next press re-pins, and
    -- teardown clears every monitor.
end

function HYPRZOOM.teardown()
    HYPRZOOM.held = false
    unbind_all(WHEEL_UP)
    unbind_all(WHEEL_DOWN)
    unbind_all(cfg.button)
    pcall(plugin.unpin)
    set({
        zoom_factor          = 1.0,
        zoom_rigid           = HYPRZOOM.saved.zoom_rigid,
        zoom_detached_camera = HYPRZOOM.saved.zoom_detached_camera,
        zoom_disable_aa      = HYPRZOOM.saved.zoom_disable_aa,
    })
    HYPRZOOM = nil
end

pcall(plugin.sens_exponent, cfg.sens_exponent)

set({
    zoom_rigid           = cfg.rigid,
    zoom_detached_camera = cfg.detached_camera,
    zoom_disable_aa      = cfg.disable_aa,
})

-- ignore_mods is not optional here. A bind with no modifiers only matches when
-- none are held, and a game keeps Shift and Ctrl down constantly — without it
-- the zoom refuses to start under crouch or walk, and worse, a modifier pressed
-- mid-hold makes the release bind miss and the zoom stick.
hl.bind(cfg.button, press, { description = "hyprzoom:press", ignore_mods = true })
hl.bind(cfg.button, release, { release = true, description = "hyprzoom:release", ignore_mods = true })
