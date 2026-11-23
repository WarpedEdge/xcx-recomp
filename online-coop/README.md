# Online co-op

This folder holds the add-on for playing the game together over a network. It is built as a Lua mod with a native provider, so it uses the mod page and the native Online screens from the mod system and doesn't change the game's own code.

Right now it can host and join a session on your own machine (loopback), pass a party invite and echo messages between two peers. Two games can also show each other's position (see Position link below). Joining over the internet comes later, one step at a time.

## Layout

| Folder | What's in it |
| --- | --- |
| `lua/` | The mod: `mod.ini` and `main.lua`. It registers the Online entry and the squad screens, and starts and stops the provider. |
| `provider/` | The native provider (`provider.cpp`), its export list and the pose reader (`game_pose.h`). It uses the provider headers in `runtime/`. |
| `src/`, `include/` | The transport, the message format and the party state, in C++17. No game code is in here. |
| `tools/` | `build_provider.py` builds the provider and assembles a mod folder from `lua/`. `peer.cpp` is a small test peer. |
| `tests/` | Tests for the message format, the party state, the pose reader, the network code, the provider and the Lua mod. |

## Position link

Two games can show where the other player stands. Set `XCX_COOP_POSE_PORT=<port>` to listen and `XCX_COOP_POSE_PEER=<ip>:<port>` to say where to send, as environment variables or as `COOP_POSE_PORT` and `COOP_POSE_PEER` in `xcx.ini`. Each game then sends its player's position and whether the player is in a fight about 20 times a second and holds the other player's position on its second party member, as long as you're both in the same area. Two games count as in the same area when the game's area numbers match or the other player is within 400 units, because neighboring blocks of the same ground have different numbers.

`XCX_COOP_FOLLOW=1` (or `COOP_FOLLOW` in `xcx.ini`) is for tests and is off by default. When the other player has been in another area for 3 seconds, this game jumps to that area through the game's own jump table, then moves the player to the other player's position. It tries three times for each area and knows the entry rows for the areas from 5 to 77 and from 192 to 383. A joining action the player chooses will replace it.

While the other player is connected, the death countdown is held at its start value, so a downed player stays down. `XCX_COOP_TIMER=normal` (or `COOP_TIMER` in `xcx.ini`) lets the game's countdown run. It is the same hold that was checked in a separate test build; in this build it has fired once in a run, and a fight death has not been run with it yet.

Teleport actions between the two games (ask to go to the other player, ask the other player to come, go now, move the other player here now, answer yes or no) use a small 32-byte control message over the same link; `online_coop_command` in `runtime/online_coop_adapter.h` is what a menu will call. The ask versions expire after 30 s, and a question waits while this player is in a fight the other is not in. Until the menu exists, `XCX_COOP_TEST_CMD=<go|bring|force_go|force_bring|accept|decline>@<seconds>` runs one action that many seconds after the first pass and `XCX_COOP_AUTO_ACCEPT=1` answers yes, for tests.

A Lua mod calls these through `host.native_ui.coop_command(name)` (`go`, `bring`, `force_go`, `force_bring`, `accept`, `decline`; see `docs/mods/05-native-ui-hooks.md`). The call goes to the registry in `runtime/mod_native_ui.c`; the game thread takes the command at its next pass and passes it to `online_coop_command`. A button on the mod's page can use it once the page declares the actions.

The squad screens open and close the position link. While a squad session is up (the native screens report it), the add-on reads the squad's passcode and role from the registry (`mod_native_ui_details`) every 16th pass. In the first stage the passcode is the host's loopback port P (1024 to 65533): the player who created the squad listens on P + 1 and sends to P + 2 on `127.0.0.1`, the player who joined does the opposite (`coop_session_ports` in `runtime/online_coop_pose.c`). When the session ends the link is closed and the other player's character is no longer shown. A link set by the `XCX_COOP_POSE_PORT` and `XCX_COOP_POSE_PEER` settings is left alone. For tests, `XCX_COOP_TEST_SESSION=host:<port>` or `join:<port>` acts as if that session were up.

`XCX_COOP_JOIN=<distance>` is off by default. While both players are in a fight and are more than 25 and at most that many units apart, the player who entered the fight later is moved next to the other, at most once every 20 s. `XCX_COOP_TEST_FIGHT=<seconds>` reports a made-up fight from that time on, for tests.

Without these settings nothing is sent and no port is opened. It only works on the supported 2.0.0 build, and it has only been run on Linux so far. The hooks it uses are `coop.tick` and `coop.node_copy` in `recomp/native_ui.py`, and the game side is `runtime/online_coop_adapter.c`.

## Build and test

Run these from this folder. Build output goes to `build/online-coop/` at the top of the repository.

```text
make test
```

builds the transport and runs the format, party, pose, pose link and network tests. They need only a C and a C++17 compiler and Python.

```text
python3 tests/dynamic/check.py
python3 tests/dynamic/check_lua.py
```

build the provider with clang, load it through the mod loader and run the Lua mod against it. `check_lua.py` needs the Lua sources in `build/lua-src`. Unset `LD_LIBRARY_PATH`, `LD_PRELOAD` and `LD_AUDIT` first, because the mod loader refuses native providers when they are set. On Windows, `tests/dynamic/windows-check.ps1` does the same.

```text
NATIVE_TEST_EXE=<path to the native UI test host> python3 -B -m unittest tests/package/test_coop_package.py
```

runs the Lua mod through the native UI test host (`python tests/native_ui/check.py` builds it).

None of these tests needs the game or its files.
