#include "genesis_launcher_netplay.h"

#include <stdio.h>
#include <string.h>

#include "recomp_net/recomp_net.h"
#include "recomp_net/lobby_client.h"

typedef struct LauncherNetplayState {
    char game_name[64];
    char game_version[32];
    char lobby_url[256];
    char registry_path[512];
    int hosting_lan;
    int joined_lan;
    RecompLauncherCNetplayLaunch pending_lan;
    RNetIpv4Address addresses[16];
    int address_count;
    int launch_ice_hub;   /* the launch fill_launch last answered rode ICE agents */
    int launch_is_host;
    char error[64];
} LauncherNetplayState;

static LauncherNetplayState g_launcher_np;

/* Genesis-owned match_caps keys, carried in RNetLobbyMatchCaps.ext. The wire
 * names are the ones the retired forked client used, so a lobby already
 * holding them stays readable. */
typedef struct GenesisCapsExt {
    int widescreen;
    int widescreen_cells;
    int pad_mode[2]; /* 0 = 3-button, 1 = 6-button */
} GenesisCapsExt;

static int caps_write_extra(const RNetLobbyMatchCaps *caps, char *out,
                            size_t cap, void *ctx)
{
    GenesisCapsExt ext;
    int n;
    (void)ctx;
    memcpy(&ext, caps->ext.bytes, sizeof(ext));
    n = snprintf(out, cap,
                 ",\"widescreen\":%s,\"widescreen_cells\":%d,"
                 "\"pad_mode_p1\":%d,\"pad_mode_p2\":%d",
                 ext.widescreen ? "true" : "false", ext.widescreen_cells,
                 ext.pad_mode[0] ? 1 : 0, ext.pad_mode[1] ? 1 : 0);
    return n < 0 || (size_t)n >= cap ? -1 : n;
}

static void caps_parse_extra(const char *json, RNetLobbyMatchCaps *caps,
                             void *ctx)
{
    GenesisCapsExt ext;
    (void)ctx;
    memset(&ext, 0, sizeof(ext));
    ext.widescreen = rnet_lobby_json_get_bool(json, "widescreen", 0);
    ext.widescreen_cells = rnet_lobby_json_get_int(json, "widescreen_cells", 0);
    ext.pad_mode[0] = rnet_lobby_json_get_int(json, "pad_mode_p1", 0) ? 1 : 0;
    ext.pad_mode[1] = rnet_lobby_json_get_int(json, "pad_mode_p2", 0) ? 1 : 0;
    memcpy(caps->ext.bytes, &ext, sizeof(ext));
}

static RNetLobbyMatchCaps caps_from_settings(const RecompLauncherCSettings *settings)
{
    RNetLobbyMatchCaps caps;
    GenesisCapsExt ext;
    memset(&caps, 0, sizeof(caps));
    memset(&ext, 0, sizeof(ext));
    caps.valid = 1;
    caps.input_delay = 2;
    /* Genesis netplay is delay-synchronised only: no rollback, no input
     * prediction. Say so rather than inherit the lobby's rollback default. */
    caps.rollback = 0;
    /* Host relay (over ICE when the build has it): the host's preference. */
    caps.relay_host = rnet_lobby_relay_host_pref();
    caps.relay_via_ice = rnet_lobby_relay_via_ice();
    if (settings) {
        ext.widescreen = settings->widescreen != 0;
        ext.widescreen_cells = settings->widescreen_cells;
        ext.pad_mode[0] = settings->pad_mode[0] == 1;
        ext.pad_mode[1] = settings->pad_mode[1] == 1;
    }
    memcpy(caps.ext.bytes, &ext, sizeof(ext));
    return caps;
}

static int read_lan(RNetLanLobby *out)
{
    return rnet_lan_lobby_read(g_launcher_np.registry_path,
                               g_launcher_np.game_name,
                               g_launcher_np.game_version, out) == RNET_LAN_LOBBY_OK;
}

static const char *np_default_url(void *ctx)
{
    (void)ctx;
    return g_launcher_np.lobby_url[0] ? g_launcher_np.lobby_url
                                      : rnet_lobby_default_url();
}

static void np_set_lobby_url(void *ctx, const char *url)
{
    (void)ctx;
    snprintf(g_launcher_np.lobby_url, sizeof(g_launcher_np.lobby_url), "%s",
             url && url[0] ? url : rnet_lobby_default_url());
}

static int np_connect(void *ctx)
{
    (void)ctx;
    rnet_lobby_set_game_identity(g_launcher_np.game_name,
                                    g_launcher_np.game_version);
    return rnet_lobby_connect(np_default_url(NULL));
}
static int np_connected(void *ctx) { (void)ctx; return rnet_lobby_connected(); }
static void np_pump(void *ctx) { (void)ctx; rnet_lobby_pump(); }
static void np_set_player_name(void *ctx, const char *name)
{
    (void)ctx;
    rnet_lobby_set_display_name(name && name[0] ? name : "Player");
}
static const char *np_player_name(void *ctx)
{
    (void)ctx;
    return rnet_lobby_display_name();
}
static void np_request_list(void *ctx) { (void)ctx; rnet_lobby_request_list(); }

static int np_list_count(void *ctx)
{
    RNetLanLobby lan;
    (void)ctx;
    return rnet_lobby_list_count() + (read_lan(&lan) ? 1 : 0);
}

static int np_list_get(void *ctx, int index, RecompLauncherCNetplayLobby *out)
{
    int remote_count;
    (void)ctx;
    if (!out) return 0;
    remote_count = rnet_lobby_list_count();
    if (index >= remote_count) {
        RNetLanLobby lan;
        if (!read_lan(&lan)) return 0;
        strcpy(out->lobby_id, "lan:local");
        snprintf(out->name, sizeof(out->name), "%s", lan.name);
        snprintf(out->game_name, sizeof(out->game_name), "%s", lan.game);
        snprintf(out->game_version, sizeof(out->game_version), "%s", lan.game_version);
        out->player_count = lan.joiner_name[0] ? 2 : 1;
        out->max_slots = 2;
        out->has_password = lan.password[0] != '\0';
        return 1;
    } else {
        RNetLobbyRow row;
        if (!rnet_lobby_list_get(index, &row)) return 0;
        snprintf(out->lobby_id, sizeof(out->lobby_id), "%s", row.lobby_id);
        snprintf(out->name, sizeof(out->name), "%s", row.name);
        snprintf(out->game_name, sizeof(out->game_name), "%s", row.game_name);
        snprintf(out->game_version, sizeof(out->game_version), "%s", row.game_version);
        out->player_count = row.player_count;
        out->max_slots = row.max_slots;
        out->has_password = row.has_password;
        return 1;
    }
}

static void refresh_addresses(void)
{
    int count = rnet_ipv4_enumerate(g_launcher_np.addresses,
                                    sizeof(g_launcher_np.addresses) /
                                    sizeof(g_launcher_np.addresses[0]));
    g_launcher_np.address_count = count < 0 ? 0 :
        (count > 16 ? 16 : count);
}

static int np_local_ip(void *ctx, char *out, size_t out_len)
{
    (void)ctx;
    if (!out || !out_len) return 0;
    refresh_addresses();
    if (!g_launcher_np.address_count) return 0;
    snprintf(out, out_len, "%s", g_launcher_np.addresses[0].address);
    return 1;
}

static int np_external_ip(void *ctx, char *out, size_t out_len)
{
    (void)ctx;
    return rnet_external_ipv4_discover(NULL, out, out_len) == RNET_EXTERNAL_IPV4_OK;
}

static int np_local_address_get(void *ctx, int index,
                                RecompLauncherCNetplayLocalAddress *out)
{
    (void)ctx;
    if (!out || index < 0) return 0;
    if (index == 0) refresh_addresses();
    if (index >= g_launcher_np.address_count) return 0;
    snprintf(out->address, sizeof(out->address), "%s",
             g_launcher_np.addresses[index].address);
    snprintf(out->label, sizeof(out->label), "%s",
             g_launcher_np.addresses[index].interface_label);
    return 1;
}

static int np_create(void *ctx, const char *lobby_name, char *host_endpoint,
                     const char *password, const RecompLauncherCSettings *settings,
                     int lan_only, int max_slots)
{
    RNetLobbyMatchCaps caps = caps_from_settings(settings);
    RNetLanLobby lan;
    const char *endpoint = host_endpoint && host_endpoint[0]
        ? host_endpoint : "0.0.0.0:7777";
    (void)ctx;
    (void)max_slots; /* the engine has two controller ports: always 2 */
    memset(&lan, 0, sizeof(lan));
    snprintf(lan.name, sizeof(lan.name), "%s",
             lobby_name && lobby_name[0] ? lobby_name : "Genesis Netplay");
    snprintf(lan.game, sizeof(lan.game), "%s", g_launcher_np.game_name);
    snprintf(lan.game_version, sizeof(lan.game_version), "%s", g_launcher_np.game_version);
    snprintf(lan.endpoint, sizeof(lan.endpoint), "%s", endpoint);
    snprintf(lan.host_name, sizeof(lan.host_name), "%s",
             rnet_lobby_display_name()[0] ? rnet_lobby_display_name() : "Host");
    snprintf(lan.password, sizeof(lan.password), "%s", password ? password : "");
    if (lan_only) {
        int rc = rnet_lan_lobby_publish(g_launcher_np.registry_path, &lan);
        if (rc != RNET_LAN_LOBBY_OK) return rc;
        g_launcher_np.hosting_lan = 1;
        return 0;
    }
    return rnet_lobby_create(lan.name, g_launcher_np.game_name,
                                g_launcher_np.game_version, password ? password : "",
                                endpoint, &caps, GENESIS_NETPLAY_MAX_SEATS);
}

static int np_join(void *ctx, const char *lobby_id, const char *password,
                   char *guest_bind)
{
    (void)ctx;
    if (lobby_id && !strncmp(lobby_id, "lan:", 4)) {
        RNetLanLobby lan;
        int rc = rnet_lan_lobby_join(g_launcher_np.registry_path,
                                     g_launcher_np.game_name,
                                     g_launcher_np.game_version,
                                     password ? password : "",
                                     rnet_lobby_display_name()[0]
                                         ? rnet_lobby_display_name() : "Player",
                                     &lan);
        if (rc != RNET_LAN_LOBBY_OK) return rc;
        g_launcher_np.joined_lan = 1;
        return 0;
    }
    return rnet_lobby_join(lobby_id, password ? password : "",
                              guest_bind && guest_bind[0] ? guest_bind : "0.0.0.0:0");
}

static int np_leave(void *ctx)
{
    (void)ctx;
    if (g_launcher_np.hosting_lan)
        (void)rnet_lan_lobby_leave(g_launcher_np.registry_path, 1);
    else if (g_launcher_np.joined_lan)
        (void)rnet_lan_lobby_leave(g_launcher_np.registry_path, 0);
    g_launcher_np.hosting_lan = g_launcher_np.joined_lan = 0;
    memset(&g_launcher_np.pending_lan, 0, sizeof(g_launcher_np.pending_lan));
    return rnet_lobby_leave();
}

static int using_lan_room(void) { return g_launcher_np.hosting_lan || g_launcher_np.joined_lan; }
static int np_in_lobby(void *ctx) { (void)ctx; return using_lan_room() || rnet_lobby_in_lobby(); }
static int np_is_host(void *ctx)
{
    (void)ctx;
    return using_lan_room() ? g_launcher_np.hosting_lan : rnet_lobby_is_host();
}
static int np_member_count(void *ctx)
{
    RNetLanLobby lan;
    (void)ctx;
    if (!using_lan_room()) return rnet_lobby_member_count();
    return read_lan(&lan) ? (lan.joiner_name[0] ? 2 : 1) : 0;
}

static int np_member_get(void *ctx, int index, RecompLauncherCNetplayMember *out)
{
    (void)ctx;
    if (!out) return 0;
    if (using_lan_room()) {
        RNetLanLobby lan;
        int host;
        if (!read_lan(&lan) || index < 0 || index > (lan.joiner_name[0] ? 1 : 0)) return 0;
        host = index == 0;
        out->slot = host ? lan.host_slot : 1 - lan.host_slot;
        snprintf(out->display_name, sizeof(out->display_name), "%s",
                 host ? lan.host_name : lan.joiner_name);
        out->ready = 1;
        out->is_host = host;
        return 1;
    } else {
        RNetLobbyMember member;
        if (!rnet_lobby_member_get(index, &member)) return 0;
        out->slot = member.slot;
        snprintf(out->display_name, sizeof(out->display_name), "%s", member.display_name);
        out->ready = member.ready;
        out->is_host = member.slot == 0;
        return 1;
    }
}

static int np_move_member(void *ctx, int from_slot, int to_slot)
{
    RNetLanLobby lan;
    (void)ctx;
    if (!g_launcher_np.hosting_lan || from_slot == to_slot || !read_lan(&lan)) return -1;
    return rnet_lan_lobby_set_host_slot(g_launcher_np.registry_path,
                                        1 - lan.host_slot);
}
static int np_local_ready(void *ctx) { (void)ctx; return using_lan_room() ? 1 : rnet_lobby_local_ready(); }
static int np_all_ready(void *ctx)
{
    RNetLanLobby lan;
    (void)ctx;
    return using_lan_room() ? (read_lan(&lan) && lan.joiner_name[0])
                            : rnet_lobby_all_ready();
}
static int np_set_ready(void *ctx, int ready)
{
    (void)ctx;
    return using_lan_room() ? 0 : rnet_lobby_set_ready(ready);
}

static int np_request_start(void *ctx, const RecompLauncherCSettings *settings)
{
    RNetLobbyMatchCaps caps = caps_from_settings(settings);
    (void)ctx;
    if (g_launcher_np.hosting_lan)
        return rnet_lan_lobby_set_started(g_launcher_np.registry_path, 1);
    return rnet_lobby_request_start(&caps);
}

static int np_launch_pending(void *ctx)
{
    RNetLanLobby lan;
    (void)ctx;
    if (using_lan_room() && read_lan(&lan) && lan.started &&
        !g_launcher_np.pending_lan.enabled) {
        const char *colon;
        memset(&g_launcher_np.pending_lan, 0, sizeof(g_launcher_np.pending_lan));
        g_launcher_np.pending_lan.enabled = 1;
        g_launcher_np.pending_lan.local_slot = g_launcher_np.hosting_lan
            ? lan.host_slot : 1 - lan.host_slot;
        g_launcher_np.pending_lan.input_player = 0;
        g_launcher_np.pending_lan.session_id = 1;
        g_launcher_np.pending_lan.input_delay = 2;
        if (g_launcher_np.hosting_lan) {
            colon = strrchr(lan.endpoint, ':');
            snprintf(g_launcher_np.pending_lan.bind_hostport,
                     sizeof(g_launcher_np.pending_lan.bind_hostport),
                     "0.0.0.0:%s", colon ? colon + 1 : "7777");
        } else {
            strcpy(g_launcher_np.pending_lan.bind_hostport, "0.0.0.0:0");
            snprintf(g_launcher_np.pending_lan.peer_hostport,
                     sizeof(g_launcher_np.pending_lan.peer_hostport), "%s", lan.endpoint);
        }
    }
    return g_launcher_np.pending_lan.enabled || rnet_lobby_launch_pending();
}

static void np_clear_launch_pending(void *ctx)
{
    (void)ctx;
    memset(&g_launcher_np.pending_lan, 0, sizeof(g_launcher_np.pending_lan));
    rnet_lobby_clear_launch_pending();
}

static int np_fill_launch(void *ctx, RecompLauncherCNetplayLaunch *out)
{
    RNetLobbyJoinInfo join;
    const RNetLobbyMatchCaps *caps;
    int cap;
    (void)ctx;
    if (!out) return 0;
    if (g_launcher_np.pending_lan.enabled) {
        g_launcher_np.launch_ice_hub = 0;
        *out = g_launcher_np.pending_lan;
        return 1;
    }
    /* Needs launch_pending: a launch the lobby client refused (an ICE seat
     * that never connected -> last_error "ice_not_connected") is never
     * started, so no smaller room can boot. */
    if (!rnet_lobby_try_fill_launch(&join)) return 0;
    if (join.force_input_relay) {
        /* The lobby server's UDP input relay is not implemented for Genesis
         * (it never was); say so instead of dialling a peer that is not there. */
        snprintf(g_launcher_np.error, sizeof(g_launcher_np.error),
                 "relay_unsupported");
        rnet_lobby_clear_launch_pending();
        return 0;
    }
    if (join.transport_ice_hub) {
        /* Take the agents NOW: the launcher clears launch_pending right after
         * this returns, and the lobby client then destroys an untaken bundle. */
        if (genesis_netplay_capture_ice_launch() < 0) {
            snprintf(g_launcher_np.error, sizeof(g_launcher_np.error), "%s",
                     genesis_netplay_ice_error()[0] ? genesis_netplay_ice_error()
                                                    : "ice_not_connected");
            rnet_lobby_clear_launch_pending();
            return 0;
        }
    }
    g_launcher_np.launch_ice_hub = join.transport_ice_hub ? 1 : 0;
    g_launcher_np.launch_is_host = rnet_lobby_is_host() ? 1 : 0;
    caps = rnet_lobby_match_caps();
    cap = GENESIS_NETPLAY_MAX_SEATS;
    if (join.player_count > cap) {
        snprintf(g_launcher_np.error, sizeof(g_launcher_np.error),
                 "too_many_players");
        rnet_lobby_clear_launch_pending();
        return 0;
    }
    memset(out, 0, sizeof(*out));
    out->enabled = 1;
    out->local_slot = join.transport_ice_hub ? (g_launcher_np.launch_is_host ? 0 : 1)
                                             : join.local_slot;
    out->input_player = 0;
    snprintf(out->bind_hostport, sizeof(out->bind_hostport), "%s", join.bind_hostport);
    snprintf(out->peer_hostport, sizeof(out->peer_hostport), "%s", join.peer_hostport);
    out->session_id = join.session_id;
    out->input_delay = caps && caps->valid ? caps->input_delay : 2;
    return 1;
}

static const char *np_last_error(void *ctx)
{
    const RNetLobbyJoinInfo *join;
    (void)ctx;
    if (g_launcher_np.error[0]) return g_launcher_np.error;
    join = rnet_lobby_join_info();
    return join ? join->last_error : "";
}

static void np_clear_last_error(void *ctx)
{
    (void)ctx;
    g_launcher_np.error[0] = '\0';
    rnet_lobby_clear_last_error();
}

static int np_lobby_max_slots(void *ctx)
{
    (void)ctx;
    return GENESIS_NETPLAY_MAX_SEATS;
}

static RecompLauncherCNetplayCallbacks g_callbacks = {
    NULL, np_default_url, np_set_lobby_url, np_connect, np_connected, np_pump,
    np_set_player_name, np_player_name, np_request_list, np_list_count, np_list_get,
    np_local_ip, np_external_ip, np_create, np_join, np_leave, np_in_lobby,
    np_is_host, np_member_count, np_member_get, np_move_member, np_local_ready,
    np_all_ready, np_set_ready, np_request_start, np_launch_pending,
    np_clear_launch_pending, np_fill_launch, np_local_address_get,
    /* kick_member: not offered (a two-seat room has nobody to kick but the
     * guest, who can leave) */ NULL,
    np_last_error, np_clear_last_error,
    /* input_delay_get/set, force_input_relay_get/set */ NULL, NULL, NULL, NULL,
    np_lobby_max_slots
};

void genesis_launcher_netplay_init(const char *game_name, const char *game_version,
                                   const char *lan_registry_path)
{
    memset(&g_launcher_np, 0, sizeof(g_launcher_np));
    snprintf(g_launcher_np.game_name, sizeof(g_launcher_np.game_name), "%s",
             game_name && game_name[0] ? game_name : "Genesis");
    snprintf(g_launcher_np.game_version, sizeof(g_launcher_np.game_version), "%s",
             game_version && game_version[0] ? game_version : "dev");
    snprintf(g_launcher_np.registry_path, sizeof(g_launcher_np.registry_path), "%s",
             lan_registry_path && lan_registry_path[0] ? lan_registry_path : "genesis-netplay-room.txt");
    {
        RNetLobbyConfig lc;
        RNetLobbyCapsCodec codec;
        memset(&lc, 0, sizeof(lc));
        lc.game_name = g_launcher_np.game_name;
        lc.game_version = g_launcher_np.game_version;
        lc.platform = "genesis";
        lc.max_players = GENESIS_NETPLAY_MAX_SEATS; /* two controller ports */
        lc.automatch_slots = 2;
        lc.legacy_env_prefix = "GENESIS_NET_"; /* GENESIS_NET_LOBBY_URL still works */
        rnet_lobby_configure(&lc);
        memset(&codec, 0, sizeof(codec));
        codec.write_extra = caps_write_extra;
        codec.parse_extra = caps_parse_extra;
        rnet_lobby_set_caps_codec(&codec);
    }
    rnet_lobby_set_game_identity(g_launcher_np.game_name, g_launcher_np.game_version);
}

const RecompLauncherCNetplayCallbacks *genesis_launcher_netplay_callbacks(void)
{
    return &g_callbacks;
}

void genesis_launcher_netplay_apply_host_caps(RecompLauncherCSettings *settings)
{
    const RNetLobbyMatchCaps *caps = rnet_lobby_match_caps();
    if (!settings || !caps || !caps->valid || rnet_lobby_is_host()) return;
    GenesisCapsExt ext;
    memcpy(&ext, caps->ext.bytes, sizeof(ext));
    settings->widescreen = ext.widescreen;
    settings->widescreen_cells = ext.widescreen_cells;
    settings->pad_mode[0] = ext.pad_mode[0] ? 1 : 0;
    settings->pad_mode[1] = ext.pad_mode[1] ? 1 : 0;
}

void genesis_launcher_netplay_config_from_launch(
    const RecompLauncherCSettings *settings, GenesisNetplayConfig *config)
{
    if (!config) return;
    genesis_netplay_config_defaults(config);
    if (!settings || !settings->netplay_launch.enabled) return;
    config->enabled = 1;
    config->local_slot = settings->netplay_launch.local_slot;
    config->input_player = settings->netplay_launch.input_player;
    config->input_delay = settings->netplay_launch.input_delay;
    config->session_id = settings->netplay_launch.session_id;
    snprintf(config->bind_hostport, sizeof(config->bind_hostport), "%s",
             settings->netplay_launch.bind_hostport);
    snprintf(config->peer_hostport, sizeof(config->peer_hostport), "%s",
             settings->netplay_launch.peer_hostport);
    config->ice_hub = g_launcher_np.launch_ice_hub;
}
