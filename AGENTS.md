# XCX Recomp

Static recompilation of *Xenoblade Chronicles X: Definitive Edition* (Switch, ARM64) into a native Windows x86-64 exe with a Vulkan renderer. Design: [docs/architecture.md](docs/architecture.md). Toolchain, build commands and tests: [docs/building.md](docs/building.md).

## Game content boundary

This repository is public. Game data lives outside it and every derived file stays in ignored folders.

- The user's game and key paths are in `config.local.toml` (ignored). Pass the keys file path to tools; the tools read it themselves. Key values stay out of your output, logs, commands and files.
- Everything extracted or generated goes under `build/` or `generated/`.
- Every file the code generator emits carries the marker defined as `GENERATED_MARKER` in `tools/check_content.py`.
- Run `python tools/check_content.py --staged` before proposing a commit. It is the same check CI runs.

## Reusing code

`refs/` holds the upstream checkouts (`tools/fetch_refs.sh` recreates it). Read-only.

- Copy only MIT, BSD, ISC, zlib or Apache-2.0 code, keep its header, and add a row to `THIRD_PARTY_NOTICES.md`.
- yuzu, suyu, OpenSmash-Ultimate and other GPL code: read for behaviour, write our own.
- `tools/vendor/` stays byte-identical to upstream; Windows changes go in `tools/extract.py`.

## Code

- Build C with MSYS2 UCRT64 Clang (`C:\msys64\ucrt64\bin`). Generated C uses GNU extensions, so `cl.exe` is out. Call Ninja from that directory by full path or put it first on PATH.
- Two Python environments: `.venv` (3.12) for the lifter and the build, `.venv-py314` for the extraction and shader scripts, which need `compression.zstd`.
- Generated C is never edited by hand: change the generator (`recomp/`).
- Every lifter change passes the differential tests against Unicorn ([docs/building.md](docs/building.md), "Checks"). After changing `recomp/jumptables.py`, `tests\jumptables_check.py` reports 0 disagreements on 1.2.1 and 1.4.3.
- `recomp/layout.py` alone decides module bases.
- Code on a hot path (a draw call, a texture or image the game submits, a pipeline or shader lookup, the per-frame update, input polling) runs hundreds of times a frame, so whatever it costs is multiplied. Keep the work per call small and constant: return first when the feature is off or the call is not one you handle, and do anything costly (hashing or comparing an image, looking up a name, parsing) once per resource and cache the answer, or only when something changes (a menu opens, a cursor moves). Features that draw over or into the game, such as button glyphs, icons and overlays, must not inspect every image or draw the game submits to find their target, and must not rebuild or redraw their pictures every frame: find the target once (by the screen or menu that shows it, or by the resource when it is created), keep the finished picture, and redraw only when its content or place changes. This went wrong once: the button icon styles in 0.1.18 checked every image the game drew, hundreds of times a frame, and cost a third of the frame rate (issue 136; 140 fps in 0.1.16, 88 in 0.1.18 in one scene). Before a change that adds such a hook or such drawing, and after it, measure the frame rate in the same scene and give both numbers in the commit or pull request description.
- Graphics bugs: [docs/maxwell.md](docs/maxwell.md) lists the Maxwell behaviour the Vulkan path must match and the XCX bugs other ports fixed.
- Comments say what the code does and why, in words that stand on their own. Cite our code by file plus function (a line number may follow as a hint) and upstream code by project, file and function: `refs/` moves, so its line numbers rot. Commit hashes belong to the attribution of copied code.

## Writing

Code, comments, docs and commit messages are in English. Commit messages are plain sentences without type prefixes.
