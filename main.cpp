#define WLR_USE_UNSTABLE

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/MonitorStateTracker.hpp>
#include <hyprland/src/state/MonitorQuery.hpp>

// Hyprland's cursor zoom always anchors on the pointer: MonitorZoomController
// reads the cursor position every time the zoom factor changes. There is no
// config option to anchor it anywhere else — the internal flag that would centre
// it (mouseZoomUseMouse = false) is only used by the monitor-hotplug animation.
//
// Warping the cursor to the centre first does not help under a game: a client
// holding a pointer lock gets the cursor put back on the same call, because
// moveCursor runs simulateMouseMovement() and the constraint handler warps to
// the client's position hint.
//
// So this plugin uses the one escape hatch the controller already has —
// pinAnchor(), otherwise only reachable from the trackpad pinch gesture — to
// nail the anchor to the centre of the monitor. With the anchor pinned the
// cursor stops mattering entirely, no warping and no fighting the constraint.

inline HANDLE PHANDLE = nullptr;

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static PHLMONITOR monitorUnderCursor() {
    return State::monitorState()->query().vec(Pointer::mgr()->position()).run();
}

// Anchors are monitor-local: getAnchor() returns cursor - monitor->m_position,
// so the centre is simply half the monitor size.
static SDispatchResult onPin(std::string) {
    const auto MONITOR = monitorUnderCursor();

    if (!MONITOR)
        return SDispatchResult{.success = false, .error = "zoomcenter: no monitor under cursor"};

    MONITOR->m_zoomController.pinAnchor(MONITOR->m_size / 2.0);
    return {};
}

// Clear every monitor, not just the current one: the cursor may have crossed to
// another monitor between pin and unpin, which would otherwise leave one pinned.
static SDispatchResult onUnpin(std::string) {
    for (const auto& MONITOR : State::monitorState()->monitors()) {
        MONITOR->m_zoomController.clearAnchor();
    }
    return {};
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, "[zoomcenter] Version mismatch: headers differ from the running Hyprland", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("[zoomcenter] Version mismatch");
    }

    HyprlandAPI::addDispatcherV2(PHANDLE, "zoomcenter:pin", onPin);
    HyprlandAPI::addDispatcherV2(PHANDLE, "zoomcenter:unpin", onUnpin);

    return {"zoomcenter", "Anchor the cursor zoom to the centre of the monitor", "hyprfox", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    onUnpin("");
}
