# Online co-op

This folder holds the add-on for playing the game together over a network. It is built as a Lua mod with a native provider, so it uses the mod page and the native Online screens from the mod system and doesn't change the game's own code.

Right now it can host and join a session on your own machine (loopback), pass a party invite and echo messages between two peers. Sending positions, showing the other player and joining over the internet come later, one step at a time.

## Layout

| Folder | What's in it |
| --- | --- |
| `lua/` | The mod: `mod.ini` and `main.lua`. It registers the Online entry and the squad screens, and starts and stops the provider. |
| `provider/` | The native provider (`provider.cpp`), its export list and the pose reader (`game_pose.h`). It uses the provider headers in `runtime/`. |
| `src/`, `include/` | The transport, the message format and the party state, in C++17. No game code is in here. |
| `tools/` | `build_provider.py` builds the provider and assembles a mod folder from `lua/`. `peer.cpp` is a small test peer. |
| `tests/` | Tests for the message format, the party state, the pose reader, the network code, the provider and the Lua mod. |

## Build and test

Run these from this folder. Build output goes to `build/online-coop/` at the top of the repository.

```text
make test
```

builds the transport and runs the format, party, pose and network tests. They need only a C++17 compiler and Python.

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
