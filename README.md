# hypr-zoom-center

A Hyprland plugin that pins the cursor zoom to the centre of the monitor.

## Why

`cursor:zoom_factor` always magnifies around the pointer. `MonitorZoomController`
re-reads the cursor position every time the factor changes, and no config value
changes that — the internal flag that would centre it (`mouseZoomUseMouse = false`)
is used only by the monitor-hotplug animation.

Warping the cursor to the centre before zooming works on the desktop but not under
a game. A client holding a pointer lock gets the cursor put straight back: the
`movecursor` action calls `simulateMouseMovement()`, and the constraint handler
warps to the client's own position hint on that same call.

The controller does have an escape hatch — `pinAnchor()`, otherwise only used by
the trackpad pinch gesture. This plugin exposes it. With the anchor pinned the
cursor stops mattering: no warping, no fighting the constraint, and the view does
not drift while you move the mouse.

## Dispatchers

| Dispatcher | Effect |
|---|---|
| `zoomcenter:pin` | Pin the zoom anchor to the centre of the monitor under the cursor |
| `zoomcenter:unpin` | Release the anchor on every monitor |

The plugin does not touch `cursor:zoom_factor`; drive that yourself. Pin before
raising the factor — the anchor is read when the factor changes.

## Install

```sh
hyprpm add /path/to/hypr-zoom-center
hyprpm enable zoomcenter
```

Rebuild after a Hyprland update with `hyprpm update`.

## Usage

Paired with `~/.config/caelestia/zoom.lua`, which binds hold-to-zoom to a side
mouse button and calls these dispatchers around the factor change.
