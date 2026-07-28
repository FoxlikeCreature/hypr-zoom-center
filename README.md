# hypr-zoom-center

Hold a mouse button, magnify the monitor you are looking at, let go. The zoom
stays centred on the monitor and does not chase the pointer — which is the part
Hyprland cannot do on its own.

Three pieces:

| Path | Role |
|---|---|
| `main.cpp` | Hyprland plugin. Pins the zoom anchor to the centre of the monitor |
| `config/zoom.lua` | The behaviour: binds, zoom level, wheel adjustment. Lives in Hyprland's Lua VM |
| `bin/zoom` | Switch: `zoom on` / `off` / `status`, or no argument to toggle |

## Why a plugin is needed

`cursor:zoom_factor` always magnifies around the pointer. `MonitorZoomController`
re-reads the cursor position every time the factor changes, and no config value
changes that — the internal flag that would centre it (`mouseZoomUseMouse = false`)
is used only by the monitor-hotplug animation.

Warping the cursor to the centre first works on the desktop but not under a game.
A client holding a pointer lock gets the cursor put straight back: the `movecursor`
action calls `simulateMouseMovement()`, and the constraint handler warps to the
client's own position hint on that same call. Measured against a running game, the
cursor does not move by a single pixel.

The controller does have an escape hatch — `pinAnchor()`, otherwise only used by
the trackpad pinch gesture. This plugin exposes it. With the anchor pinned the
cursor stops mattering: no warping, no fighting the constraint, and the view does
not drift while you move the mouse.

## Install

```sh
hyprpm add https://github.com/FoxlikeCreature/hypr-zoom-center
hyprpm enable zoomcenter

install -Dm644 config/zoom.lua ~/.config/caelestia/zoom.lua
install -Dm755 bin/zoom ~/.local/bin/zoom
```

Rebuild after a Hyprland update with `hyprpm update`.

`zoom.lua` does not need to be sourced from your Hyprland config, and should not
be: `zoom on` loads it on demand, `zoom off` removes every bind it installed. The
path above is only where the switch looks for it.

## Use

```sh
zoom on      # install the binds
             # hold the side mouse button to magnify, wheel to adjust
zoom off     # remove them; the side button goes back to normal
```

Wheel adjustments last only while the button is held — the next press starts from
the configured factor again.

## Configuration

The `cfg` table at the top of `config/zoom.lua`:

| Field | Default | Meaning |
|---|---|---|
| `button` | `mouse:276` | `BTN_EXTRA`. The other side button is `mouse:275` |
| `factor` | `3.0` | Zoom applied on press |
| `step` | `1.15` | Multiplier per wheel notch |
| `min` / `max` | `1.0` / `10.0` | Clamp for wheel adjustment |
| `disable_aa` | `false` | Crisper but blockier upscale |

Editing takes effect on the next `zoom on`, which reloads the module.

To find your own button code, bind the candidates to a probe and press them:

```sh
hyprctl eval "for _, c in ipairs({275,276,277,278}) do
  hl.bind('mouse:'..c, function()
    local f = io.open('/tmp/btn.log','a'); f:write(c..'\n'); f:close()
  end, { description = 'btnprobe'..c })
end"
# press the buttons, read /tmp/btn.log, then:
hyprctl eval "for _, c in ipairs({275,276,277,278}) do hl.unbind('mouse:'..c) end"
```

Do not include 272–274 in that list: those are the left, right and middle buttons,
and the binds consume them.

## Plugin interface

| Call | Effect |
|---|---|
| `hl.plugin.zoomcenter.pin()` | Pin the anchor to the centre of the monitor under the cursor |
| `hl.plugin.zoomcenter.unpin()` | Release the anchor on every monitor |
| `zoomcenter:pin` / `zoomcenter:unpin` | The same, as dispatchers, for non-Lua configs |

The plugin never touches `cursor:zoom_factor`; drive that yourself. Pin before
raising the factor — the anchor is read when the factor changes.

Do not unpin the moment you drop the factor back to 1.0. Zooming out is animated,
and the frames still in flight re-read the anchor: released early, the view slides
off towards the real cursor on the way out. A pinned anchor costs nothing while
the factor is 1.0, because the transform is skipped entirely.

## Notes for Lua configs

- `hyprctl keyword` does not work at all; use `hyprctl eval "hl.config({...})"`.
- Lua globals and closures survive across `eval` calls, so state can live in the
  compositor rather than in a daemon.
- `hl.bind` / `hl.unbind` work at runtime. One key can carry a press bind and a
  release bind, and `unbind` removes one per call.
- Binds need `ignore_mods = true` to be usable in a game. A bind with no
  modifiers matches only when none are held, and a game keeps Shift and Ctrl down
  constantly — without it the zoom refuses to start while crouching, and a
  modifier pressed mid-hold makes the release bind miss and the zoom stick on.
