#ifndef GENESIS_NETPLAY_H
#define GENESIS_NETPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Enforced seat ceiling. The Genesis machine model (runner/video/genesis_machine.c,
 * machine_set_pad) has exactly two controller ports and does not emulate a
 * multitap / J-Cart, so the engine has nowhere to put a third pad. The cap is
 * enforced, not advisory: the lobby is created with max_slots 2, a hub
 * handover carrying anything but one guest is refused, and a local_slot
 * outside 0..1 fails the start instead of being coerced.
 */
#define GENESIS_NETPLAY_MAX_SEATS 2

struct RNetIceAgent;

typedef struct GenesisNetplayConfig {
    int      enabled;
    int      local_slot;
    int      input_player;
    int      input_delay;
    uint32_t session_id;
    char     bind_hostport[64];
    char     peer_hostport[64];
    /* 0 = automatic, 1 = force ICE, 2 = force LAN. */
    int      transport;
    /* 1 = the launch rode ICE agents the waiting room already connected
     * (transport_ice_hub). The agent must have been handed over with
     * genesis_netplay_capture_ice_launch / genesis_netplay_stash_ice_agent
     * before genesis_netplay_start. Session slot 0 is the lobby host. */
    int      ice_hub;
} GenesisNetplayConfig;

void genesis_netplay_config_defaults(GenesisNetplayConfig *cfg);
void genesis_netplay_apply_env(GenesisNetplayConfig *cfg);

int      genesis_netplay_active(void);
int      genesis_netplay_is_running(void);
int      genesis_netplay_local_slot(void);
int      genesis_netplay_input_player(void);
uint32_t genesis_netplay_sim_tick(void);

/*
 * Host relay over ICE handover. Call from the launcher's fill_launch, which
 * runs BEFORE rnet_lobby_clear_launch_pending() (the lobby client destroys an
 * untaken bundle after that). Idempotent for one launch.
 * Returns 1 when an agent is now stashed, 0 when the launch is not an ICE hub
 * launch (nothing done), -1 when it is one but cannot be run (reason via
 * genesis_netplay_ice_error()).
 */
int  genesis_netplay_capture_ice_launch(void);
/* Hand over one already-connected agent directly (automation / tests). The
 * stash takes ownership on success; -1 leaves it with the caller. */
int  genesis_netplay_stash_ice_agent(struct RNetIceAgent *agent);
const char *genesis_netplay_ice_error(void);

int  genesis_netplay_start(const GenesisNetplayConfig *cfg);
void genesis_netplay_shutdown(void);

int  genesis_netplay_needs_local_sample(void);
void genesis_netplay_stage_local(uint16_t buttons);
int  genesis_netplay_poll_admit(void);
void genesis_netplay_wait_recv(int timeout_ms);
void genesis_netplay_finish_frame(void);

uint16_t genesis_netplay_published_pad(int slot);
int genesis_netplay_input_desync(uint32_t *tick, uint32_t *local_hash,
                                 uint32_t *remote_hash);
int genesis_netplay_peer_disconnected(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
