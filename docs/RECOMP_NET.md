# Genesis netplay host integration

`segagenesisrecomp` vendors `recomp-net` at `external/recomp-net` and exposes an
opt-in two-player delay-synchronized runtime. Game targets enable it after
creating the executable:

```cmake
include("${RECOMP_ROOT}/cmake/GenesisRecompNetplay.cmake")
genesisrecomp_enable_netplay(MyGame GAME_VERSION "dev" ICE)
```

The launcher supplies hosted-lobby or direct-LAN parameters. Automation can
bypass the launcher with these variables:

- `GENESIS_NETPLAY=1`
- `GENESIS_NET_SLOT=0|1`
- `GENESIS_NET_BIND=host:port`
- `GENESIS_NET_PEER=host:port` (empty is valid for the listening host)
- `GENESIS_NET_SESSION_ID=number`
- `GENESIS_NET_DELAY=0..16`
- `GENESIS_NET_INPUT_PLAYER=0|1`
- `GENESIS_NET_TRANSPORT=lan|ice|ice-hub` (`ice-hub` adopts an agent previously
  handed to `genesis_netplay_stash_ice_agent`; it is for automation and tests)
- `GENESIS_NET_LOBBY_URL=ws://host:port`

The frame contract is strict: stage one local pad, pump until admission,
publish both slot inputs, run exactly one emulated frame, then advance. During
a locked session the published inputs are the only controller source. Save
states are disabled because this first milestone does not synchronize state.

Games whose two-player mode uses a stacked double-height framebuffer can pass
`PEER_VIEW` to `genesisrecomp_enable_netplay`. While netplay is active and a
double-height frame is present, slot 0 displays the top half and slot 1 the
bottom half. This is presentation-only: the complete native framebuffer is
still used for hashes, screenshots, synchronization, and savestates.

## Seat cap: two players, enforced

The machine model has two controller ports (`machine_set_pad` takes port 0 or 1)
and does not emulate a multitap or J-Cart, so there is no third pad to deliver
input to. `GENESIS_NETPLAY_MAX_SEATS` is therefore 2 and is enforced, not
advisory: lobbies are created with `max_slots` 2 (`RNetLobbyConfig.max_players`
is 2), a hub handover carrying other than one guest is refused, and a
`local_slot` outside 0..1 fails `genesis_netplay_start`. The local P3/P4 masks
used by some enhanced single-machine modes are not netplay seats.

## Host relay over ICE

The lobby client is recomp-net's shared `rnet_lobby_*` client (the forked
`runner/lobby/` copy is gone; Genesis keys `widescreen`, `widescreen_cells`,
`pad_mode_p1/p2` ride its caps codec). With ICE enabled the host publishes
`relay_via: "ice"`; at launch `genesis_launcher_netplay` takes the connected
agent(s) from the lobby client inside `fill_launch` (before
`rnet_lobby_clear_launch_pending`) via `genesis_netplay_capture_ice_launch`, and
`genesis_netplay_start` adopts them: host `rnet_session_start_ice_hub_adopt`
(guest at session slot 1), guest `rnet_session_adopt_ice_agent`. Session slot 0 is
the lobby host. A launch whose guest has no ICE link is refused by the client
(`ice_not_connected`) and never starts.

Test: `cmake -S tests -B build -DGENESISRECOMP_NETPLAY=ON && cmake --build build
&& ctest --test-dir build` runs `genesis_netplay_ice`, a two-process loopback of
the real ICE hub path.
