#define WLR_USE_UNSTABLE

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/MonitorStateTracker.hpp>
#include <hyprland/src/state/MonitorQuery.hpp>

#include <lua.hpp>

#include <cmath>

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

// Sensitivity compensation.
//
// Zooming in by Z makes the same hand movement cover Z times more screen, so the
// pointer feels Z times faster. Dividing the motion by Z^exponent cancels that;
// exponent 1.0 keeps the on-screen speed exactly as it was, 0.0 disables it.
//
// input:sensitivity is useless for this. It is handed straight to libinput's
// accel_set_speed(), clamped to [-1, 1] — a curve, not a multiplier — and a game
// reading raw input takes the unaccelerated delta, which libinput's acceleration
// never touched in the first place. Both deltas have to be scaled where they are
// emitted, hence the hook below.

inline HANDLE PHANDLE = nullptr;

inline CFunctionHook* g_relativeMotionHook = nullptr;
inline float          g_sensExponent       = 1.0f;

using origRelativeMotion = void (*)(void*, uint64_t, const Vector2D&, const Vector2D&);

static float motionScale() {
    static auto PZOOMFACTOR = CConfigValue<Config::FLOAT>("cursor:zoom_factor");
    const float ZOOM        = *PZOOMFACTOR;

    if (ZOOM <= 1.0f || g_sensExponent == 0.0f)
        return 1.0f;

    return 1.0f / std::pow(ZOOM, g_sensExponent);
}

static void hkSendRelativeMotion(void* thisptr, uint64_t time, const Vector2D& delta, const Vector2D& unaccel) {
    const float SCALE = motionScale();
    const auto  ORIG  = reinterpret_cast<origRelativeMotion>(g_relativeMotionHook->m_original);

    if (SCALE == 1.0f) {
        ORIG(thisptr, time, delta, unaccel);
        return;
    }

    // Both deltas get scaled: a game may read either, and which one depends on
    // its raw input setting.
    ORIG(thisptr, time, delta * SCALE, unaccel * SCALE);
}

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

// A Lua config cannot reach plugin dispatchers, so the same two actions are also
// exported as hl.plugin.zoomcenter.pin() / .unpin().
static int luaPin(lua_State* L) {
    onPin("");
    return 0;
}

static int luaUnpin(lua_State* L) {
    onUnpin("");
    return 0;
}

// hl.plugin.zoomcenter.sens_exponent(k) — 1.0 keeps the on-screen speed constant,
// 0.0 turns compensation off, anything between softens it.
//
// Returns the current exponent and whether the motion hook actually attached,
// since a failed hook is otherwise only reported through a notification.
static int luaSensExponent(lua_State* L) {
    if (lua_isnumber(L, 1))
        g_sensExponent = static_cast<float>(lua_tonumber(L, 1));

    lua_pushnumber(L, g_sensExponent);
    lua_pushboolean(L, g_relativeMotionHook != nullptr);
    return 2;
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

    HyprlandAPI::addLuaFunction(PHANDLE, "zoomcenter", "pin", luaPin);
    HyprlandAPI::addLuaFunction(PHANDLE, "zoomcenter", "unpin", luaUnpin);
    HyprlandAPI::addLuaFunction(PHANDLE, "zoomcenter", "sens_exponent", luaSensExponent);

    // Two overloads carry this name; the one that takes Vector2D is the one the
    // input manager calls.
    for (const auto& MATCH : HyprlandAPI::findFunctionsByName(PHANDLE, "sendRelativeMotion")) {
        if (!MATCH.demangled.contains("CRelativePointerProtocol") || !MATCH.demangled.contains("Vector2D"))
            continue;

        g_relativeMotionHook = HyprlandAPI::createFunctionHook(PHANDLE, MATCH.address, reinterpret_cast<void*>(&hkSendRelativeMotion));
        break;
    }

    if (g_relativeMotionHook)
        g_relativeMotionHook->hook();
    else
        HyprlandAPI::addNotification(PHANDLE, "[zoomcenter] Could not hook relative motion; sensitivity compensation is off", CHyprColor{1.0, 0.7, 0.2, 1.0}, 5000);

    return {"zoomcenter", "Anchor the cursor zoom to the centre of the monitor", "hyprfox", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (g_relativeMotionHook)
        g_relativeMotionHook->unhook();

    onUnpin("");
}
