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

Two games can show where the other player stands. Set `XCX_COOP_POSE_PORT=<port>` to listen and `XCX_COOP_POSE_PEER=<ip>:<port>` to say where to send, as environment variables or as `COOP_POSE_PORT` and `COOP_POSE_PEER` in `xcx.ini`. Each game then sends its player's position about 20 times a second and holds the other player's position on its second party member, as long as you're both in the same area.

`XCX_COOP_FOLLOW=1` (or `COOP_FOLLOW` in `xcx.ini`) is for tests and is off by default. When the other player has been in another area for 3 seconds, this game jumps to that area through the game's own jump table, then moves the player to the other player's position. It tries three times for each area and knows the entry rows for the areas from 5 to 77 and from 192 to 383. A joining action the player chooses will replace it.

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
