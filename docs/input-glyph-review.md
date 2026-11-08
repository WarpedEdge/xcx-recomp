# Appendage glyph styles reviewer guide

Review this as an experimental adapter for one verified Appendage guide. The feature defaults to Original. It does not replace other HUD/menu/tutorial/QTE prompts, change input mappings, add Lua integration or implement colored variants. Player instructions are in [Controls](controls.md#experimental-appendage-glyphs); build identity, commands and contracts are in the [developer reference](input-glyph-styles.md).

## Suggested review order

1. **Assets and attribution:** inspect `assets/appendage-glyph/styles/provenance.json`, the original CC0 license, `THIRD_PARTY_NOTICES.md`, and both reproducible asset tools. There are 62 production tiles plus the preserved historical single-R1 fixture. PNGs are exact pinned Kenney archive members; includes are licensed-only derivatives. No game-derived atlas pixels, guest code or binaries belong in this change.
2. **Build/site validation:** read `recomp/appendage.py`, then its small `gen.py` / `nso.py` changes. Verify all six instruction words, exact main identity and original call/spill preservation. Do not infer action meanings from artwork selectors.
3. **Mapping and intent:** read `runtime/input_glyphs.c`, then the display-only snapshots and adapters in `input.c`, `input_sdl.c`, `input_win.c` and `input_wsys.h`. Check whole-chord unsupported fallback, manual/Automatic separation, exposed-device family policy and original input behavior. SDL and Windows have different source-observation adapters; Windows has not been accepted.
4. **Draw and retirement:** follow `runtime/appendage_glyph.c` through `nvn_api_cmd.c` to the four full-handle resolution points in `vk_exec_draw.c` and the private immutable images in `vk_texture.c`. Check guarded live use, retirement before teardown/reinitialization, nonrecycled scalar generations, source registration identity, upload/fence/view ownership and shutdown. There is no global atlas mutation or guest dirty-setter call.
5. **Controls and persistence:** inspect `cfg.c`, `ui/menu.cpp`, `ui_text.c` / `.h` and the user docs. Original is default, the preference is independent of pad layout/bindings, unsupported builds disable it, and fallback reasons remain visible.
6. **Fixtures and limits:** inspect the five test files and the generation boundary below. Synthetic hash/input/mutex seams do not prove guest resource ownership or scheduler races; the separate native GPU evidence covers the actual upload/view paths.

The implementation extends the preserved one-glyph prototype from baseline `b31f16572baf4518927eada59e65916b072e1359`, on reviewed master `7b2337a6854b3936dc4f567dbb46add8c1eb6559`. Local import/provenance and recovery manifests preserve the original 20-file source snapshot. No naming/controller-keyboard, Lua/Mods, Online or HUD branch code was combined. The reviewed upstream cloud patch changes are already in the baseline; `recomp/patches-upd.txt` remains byte-identical, SHA-256 `19f322dcdd26adee25bd8e0091a8456a9f861fe1b532e0fee1e33102d7d08191`.

## Generation-history assessment

The process-wide maximum is 192 successful node attachments, at most 64 complete three-node constructions. The last complete trio remains eligible after it fills the budget. A later teardown/reconstruction that leaves any slot absent cannot attach a replacement, so new binds retain original labels until restart. Partial construction can consume budget sooner; selecting Original does not stop attachments. Reusing nodes/drawing frames consumes nothing. No fixed number of enemy targets or play duration is guaranteed.

This bounds copied-command metadata and avoids unsafe record reuse while native commands may outlive a guest node. It also limits how long styles remain available in sessions that reconstruct the guide repeatedly. Controls now distinguishes terminal exhaustion from waiting for nodes and tells the player to restart. Style changes and device reconnect do not reset it. The scope remains experimental; reviewers must explicitly accept this user-visible limit. Removing it needs a separate command-lifetime design, not a larger silent cap.

The boundary fixture proves the last full trio still selects artwork at 192, the next construction uses original handles, generation count does not grow, and the status names exhaustion/restart. Earlier retirement tests protect the destroyed node's page and reject later access; copied scalar command identities remain checked independently.

## Validation evidence

Linux owner acceptance covers the implemented modes, explicitly including **Automatic and Keyboard/Mouse**. The supplied screenshots show PlayStation, Xbox and Steam Controller styling on a PlayStation controller. This is artwork/mapping acceptance, not physical Xbox/Steam hardware acceptance. The owner confirmed normal guide timeout as vanilla behavior. No repeat gameplay test is needed or requested.

| Check | Evidence and limit |
| --- | --- |
| Final focused fixtures | 4,108 policy/action/current-binding/intent/tile/status checks; ASan+UBSan enabled and disabled node fixtures passed |
| Generation/status regression | Last complete trio at cap, next construction fallback, terminal restart reason, and one copied runtime-status observation passed |
| Asset reproduction | Both includes regenerated to new ignored fixture outputs and matched byte-for-byte; all 63 curated PNGs and original license matched the pinned archive |
| Native review build | Linux incremental compile/link passed at `-j 2`; headless `--selftest` passed with zero failures; no window/GPU/game run |
| Existing full generation | 197 chunks emitted; zero unsupported instructions; only glyph chunks 34/59 and baseline cloud-patch chunk 58 differed from older output |
| Existing Unicorn differential check | 36 functions / 561 executable cases passed; 943 skipped; this is not direct execution of the six game sites |
| Existing SDL adapter fixture | Touch/pen-generated events excluded from intent while gameplay states/deltas stayed intact; genuine input and focus clearing passed |
| Existing GPU fixtures | All 62 private atlases uploaded/read back; normal and forced pixel/image/upload/view/source fallback, copied views, slot reuse, retirement and shutdown passed on RADV |
| Content and Git checks | Source/staged content scan, whitespace, pinned patch hash, preservation manifests and full read-only Git object checks passed during preparation |

The complete earlier suites were not repeated merely to reconfirm acceptance. The review fixes affect only status reporting: terminal budget exhaustion was previously described as waiting, and `glyph_status` previously read a changing runtime note twice. It now copies that result once. The test driver also accepts a separate output directory so review checks preserve older logs. Renderer selection, ownership, input behavior, resource checks, generation limit and assets are unchanged. A file-specific Git attribute preserves the original license bytes and its upstream whitespace; staged/committed bytes match the pinned archive rather than a line-ending-normalized copy.

| Candidate | SHA-256 |
| --- | --- |
| Owner-tested Linux executable | `558a95e11c3aaf75666281af609973c16c6d0ed349d8163345403f4275249265` |
| Native build with the two status fixes | `d9e04c6cf7b56dc01a4d58596f5f4b5b0a0bd550af5c717f65439476c5bb961b` |

The rebuilt binary passed focused fixtures/native selftest, but was not shown in another owner game session. The rendering/input paths accepted by the owner are unchanged; no specific owner retest is necessary for these status-only corrections. Native artifacts and logs stay ignored.

## Remaining limits

- Native Windows build/runtime and Vulkan validation-layer coverage are unverified. `VK_LAYER_KHRONOS_validation` was unavailable in the existing GPU runs; zero reported errors is not layer coverage.
- Actual Xbox/Steam hardware, arbitrary hidden Steam Input mappings, unsupported keyboard labels/bindings, general cached-command refresh, device-loss recovery and unlimited session generation history are not claimed.
- The existing 48-message binding diagnostic cap can exhaust early as three nodes alternate. Missing later diagnostic lines do not establish a rendering defect. Later upload/intent logs and owner observations remain separate evidence.
- Owner acceptance is explicit for modes, Automatic and Keyboard/Mouse. Reconnect/focus/resize were part of the requested sequence, but no independent per-step measurements were recorded. No new telemetry is invented from the overall success reply.
- Colored face buttons are a deferred appearance task. Applicable future variants should fall back to standard artwork without changing device identity or physical mappings. No variant is implemented in this PR.

## Review disposition

Ready for local PR review as a bounded, opt-in-by-preference experimental feature. The explicit generation limit, Windows gap and unavailable validation layers remain review decisions; this guide does not claim merge approval. Keep local machine recovery history, profiles, captures, extracted assets, generated guest C and binaries out of commits. No push or PR publication is part of this preparation.
