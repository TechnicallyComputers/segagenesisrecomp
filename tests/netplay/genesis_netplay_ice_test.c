#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
/*
 * Headless 2-seat loopback test of runner/netplay/genesis_netplay.c over the
 * ICE hub path (host relay over ICE).
 *
 * Two PROCESSES (never one: an in-process pair was measured desyncing, see
 * workspace CLAUDE.md section 5), forked before any thread exists:
 *
 *   host   session slot 0, adopts its one guest's agent (start_ice_hub_adopt)
 *   guest  session slot 1, adopts the one agent to the host
 *
 * The agents are produced by recomp-net's real RNetHostIce waiting-room code
 * (the module the lobby client drives), with the lobby `signal` op replaced by
 * a pipe between the processes. This test therefore proves genesis_netplay's
 * adoption, slot mapping, admission and pad publication over connected ICE
 * agents. It does NOT exercise a lobby server, the WebSocket client's launch
 * parsing, real NAT traversal, or the launcher UI.
 *
 * The runtime is configured the way automation does it: GENESIS_NETPLAY,
 * GENESIS_NET_SLOT, GENESIS_NET_DELAY, GENESIS_NET_SESSION_ID and
 * GENESIS_NET_TRANSPORT=ice-hub, through genesis_netplay_apply_env.
 *
 * Also asserts the enforced 2-seat cap: local_slot 2 fails the start.
 */
#include "genesis_netplay.h"
#include "recomp_net/recomp_net.h"
#include "recomp_net/host_ice.h"
#include "platform/rnet_platform.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FRAMES 240
#define DELAY 2
#define MSG_READY (-1)

typedef struct Msg { int type, flag; char text[2048]; } Msg;

static int g_tx, g_rx;

static uint16_t pad_for(int slot, uint32_t tick)
{
    return (uint16_t)(((slot + 1) * 0x111u + tick * 7u) & 0x0fffu);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int send_signal(const char *to, int type, int flag, const char *text, void *ctx)
{
    Msg m;
    (void)to; (void)ctx;
    memset(&m, 0, sizeof(m));
    m.type = type; m.flag = flag;
    snprintf(m.text, sizeof(m.text), "%s", text ? text : "");
    return write(g_tx, &m, sizeof(m)) == (ssize_t)sizeof(m) ? 0 : -1;
}
static int send_json(const char *json, void *ctx) { (void)json; (void)ctx; return 0; }

static int child(int is_host)
{
    RNetHostIce *ice = rnet_host_ice_create();
    RNetHostIcePeer peer;
    RNetHostIceView v;
    RNetHostIceStatus st;
    RNetIceAgent *agent;
    GenesisNetplayConfig cfg;
    const char *peer_id = is_host ? "guest" : "host";
    int peer_ready = 0, i, mismatches = 0, seen[2] = {0, 0};
    uint32_t frame = 0, hash = 2166136261u;
    char buf[32];
    double t0 = now_s();
    Msg m;

    alarm(120);
    snprintf(buf, sizeof(buf), "%d", is_host ? 0 : 1);
    setenv("GENESIS_NETPLAY", "1", 1);
    setenv("GENESIS_NET_SLOT", buf, 1);
    setenv("GENESIS_NET_DELAY", "2", 1);
    setenv("GENESIS_NET_SESSION_ID", "77", 1);
    setenv("GENESIS_NET_TRANSPORT", "ice-hub", 1);

    rnet_os_startup();
    peer.player_id = peer_id;
    peer.slot = is_host ? 1 : 0;
    memset(&v, 0, sizeof(v));
    v.active = 1; v.is_host = is_host; v.local_slot = is_host ? 0 : 1;
    v.peers = &peer; v.peer_count = 1;
    v.stun_host = ""; v.bind_address = "127.0.0.1";
    v.send_signal = send_signal; v.send_json = send_json;

    /* Waiting room: connect, then agree both ends are COMPLETED before either
     * takes its agent (the lobby client's launch gate does this on the real path). */
    for (;;) {
        int own = 0, sent_ready;
        static int told;
        if (now_s() - t0 > 60) { fprintf(stderr, "child: ICE connect timeout\n"); return 2; }
        rnet_host_ice_update(ice, &v);
        while (read(g_rx, &m, sizeof(m)) == (ssize_t)sizeof(m)) {
            if (m.type == MSG_READY) peer_ready = 1;
            else (void)rnet_host_ice_push_signal(ice, peer_id, is_host ? 1 : 0,
                                                 m.type, m.flag, m.text);
        }
        rnet_host_ice_status(ice, &st);
        own = st.completed == 1;
        sent_ready = told;
        if (own && !sent_ready) {
            memset(&m, 0, sizeof(m)); m.type = MSG_READY;
            if (write(g_tx, &m, sizeof(m)) != (ssize_t)sizeof(m)) return 2;
            told = 1;
        }
        if (own && peer_ready) break;
        rnet_os_sleep_micros(2000);
    }
    agent = rnet_host_ice_take_completed(ice, peer_id);
    if (!agent || genesis_netplay_stash_ice_agent(agent) != 0) {
        fprintf(stderr, "child: no agent to stash\n");
        return 2;
    }

    genesis_netplay_config_defaults(&cfg);
    genesis_netplay_apply_env(&cfg);
    if (!cfg.ice_hub || cfg.local_slot != (is_host ? 0 : 1)) {
        fprintf(stderr, "child: env did not configure ice-hub\n");
        return 2;
    }
    if (genesis_netplay_start(&cfg) != 0) {
        fprintf(stderr, "child: genesis_netplay_start failed (%s)\n",
                genesis_netplay_ice_error());
        return 2;
    }
    t0 = now_s();
    while (frame < FRAMES) {
        uint16_t p0, p1, e0, e1;
        if (now_s() - t0 > 60) { fprintf(stderr, "child: admit stall at tick %u\n", frame); return 3; }
        if (genesis_netplay_needs_local_sample())
            genesis_netplay_stage_local(pad_for(genesis_netplay_local_slot(),
                                                genesis_netplay_sim_tick()));
        if (!genesis_netplay_poll_admit()) { genesis_netplay_wait_recv(1); continue; }
        p0 = genesis_netplay_published_pad(0);
        p1 = genesis_netplay_published_pad(1);
        e0 = frame >= DELAY ? pad_for(0, frame - DELAY) : 0;
        e1 = frame >= DELAY ? pad_for(1, frame - DELAY) : 0;
        if (p0 != e0 || p1 != e1) {
            if (mismatches++ < 5)
                fprintf(stderr, "child %d tick %u: got %03x/%03x want %03x/%03x\n",
                        is_host ? 0 : 1, frame, p0, p1, e0, e1);
        }
        if (p0) seen[0] = 1;
        if (p1) seen[1] = 1;
        hash = (hash ^ p0) * 16777619u; hash = (hash ^ p1) * 16777619u;
        genesis_netplay_finish_frame();
        ++frame;
        if (genesis_netplay_input_desync(NULL, NULL, NULL)) { fprintf(stderr, "child: desync\n"); return 4; }
    }
    /* Keep pumping so the peer's last frames are not starved by our BYE. */
    for (i = 0; i < 300; ++i) { (void)genesis_netplay_poll_admit(); genesis_netplay_wait_recv(1); }
    genesis_netplay_shutdown();
    if (mismatches || !seen[0] || !seen[1]) return 5;
    printf("child %s ok: frames=%u hash=%08x\n", is_host ? "host" : "guest", frame, hash);
    fflush(stdout);
    return 0;
}

static int check_cap(void)
{
    GenesisNetplayConfig cfg;
    genesis_netplay_config_defaults(&cfg);
    cfg.enabled = 1;
    cfg.local_slot = 2;
    if (genesis_netplay_start(&cfg) == 0) {
        fprintf(stderr, "FAIL: local_slot 2 started (cap not enforced)\n");
        genesis_netplay_shutdown();
        return 1;
    }
    printf("PASS: local_slot 2 refused (GENESIS_NETPLAY_MAX_SEATS=%d)\n", GENESIS_NETPLAY_MAX_SEATS);
    return 0;
}

int main(void)
{
    int a2b[2], b2a[2], i, bad = 0, status;
    pid_t pid[2];
    if (check_cap()) return 1;
    fflush(stdout);
    if (pipe(a2b) || pipe(b2a)) return 1;
    for (i = 0; i < 2; ++i) {
        int *tx = i == 0 ? a2b : b2a, *rx = i == 0 ? b2a : a2b;
        pid[i] = fork();
        if (pid[i] < 0) return 1;
        if (pid[i] == 0) {
            g_tx = tx[1]; g_rx = rx[0];
            close(tx[0]); close(rx[1]);
            fcntl(g_rx, F_SETFL, fcntl(g_rx, F_GETFL) | O_NONBLOCK);
            _exit(child(i == 0));
        }
    }
    for (i = 0; i < 4; ++i) close(i == 0 ? a2b[0] : i == 1 ? a2b[1] : i == 2 ? b2a[0] : b2a[1]);
    for (i = 0; i < 2; ++i) {
        waitpid(pid[i], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            fprintf(stderr, "FAIL: %s process status %d\n", i == 0 ? "host" : "guest", status);
            bad = 1;
        }
    }
    puts(bad ? "genesis_netplay_ice_test: FAIL" : "genesis_netplay_ice_test: ok");
    return bad;
}
