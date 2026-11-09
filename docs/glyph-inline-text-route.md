# Inline text icons: traced guest route and corrected probe

Trial-only investigation on `investigate/glyph-inline-prompts-local`, baseline `2e3a10697cc5117f1a97f762d38f487594a6c46b`. No replacement is implemented and inline icons remain native. Pinned game: XCX DE 2.0.0, main BuildID `ec4ed46bbc4a9ebbe7ce0af7a4a8ebbc09e5724a`. Addresses are main-relative.

Evidence is graded, and each statement below says which grade supports it:

- **Static**: decoded from the pinned `main` with capstone by `build/glyph-icon-route-20261109/verify_sites.py` (33 checks, all pass; output in `static-verification.json`).
- **Lifted**: observed by executing the generator's own C for the guest function on synthetic memory (`tests/inline_icon_oracle.py`).
- **Fixture**: synthetic tests that mirror an assumption. They show the probe code is internally consistent; they are **not** proof about the game.
- **Runtime**: observed in a game run. The only run so far used the superseded probe.

## History

The first probe hooked the wrong vtable slot and the wrong draw pass. Its recording (exit 0, 35.76 s, preserved with a manifest at `/var/mnt/game_drive/github/glyph-text-route-recording-20261109T175642Z/`) holds **runtime** facts only about ordinary font binds: 12 events from three fonts (`…d10`, `…dd0`, `…b90`), descriptor 0×0, textures 536×676 (two) and 8244×9136, all format 1, and no layout events. "Resource equals page" compared a value with the expression that produced it and proved nothing. The earlier claim that icon layout leads to a `font_icon` bind is withdrawn.

## Established: layout method (static + lifted)

1. **Address point (static, lifted).** The layouter vtable address point is `0x1c93f08`. `f_7000a204cc` stores `[GOT 0x1d65da0] + 0x10` at `layouter+0`; the GOT slot resolves to `0x1c93ef8` by RELATIVE relocation; the two words below the address point are null. The lifted initialiser, run on a synthetic GOT page, stores exactly that value plus `0x10`. Slots: `+0x50` = `0xa23150` (tag dispatcher), `+0x78` = `0xa21750`, `+0x80` = `0xa21afc`, `+0x88` = `0xa21e94`.
2. **Tag to code (static).** The dispatcher's `ML:icon` branch (string `0x17a0aac`) loads the vptr, calls slot `+0x80` with `x0` = layouter and `x1` = the 32-bit code of the matched 0x24-byte name entry (`{u32 code, …, name[0x14] at +0x10}` in the vector at `layouter->+8->+0x58`). `[ST:icon p1=a ]` text is converted to `[ML:icon …]` by `f_7000912c14`.
3. **Entry hook word (static).** `0xa21afc` is `f9400c0a` = `ldr x10, [x0, #0x18]`: the icon source `X` at `layouter+0x18`. `x1` is masked to 16 bits.
4. **Record lookup (lifted, 48 combinations, 5 mutants rejected).** With source `X`: table pointer `X+0x38` (zero selects a fixed full-texture fallback), record count `X+0x48`, scale floats `X+0x4c/+0x50`, record offset word `table+0x18`. The record index is `code & 0xffff` when that is below the count, otherwise 0. Records are 28 bytes. For every tested code, table offset and layout mode, exactly words 0 to 5 of the selected record change the guest function's output, and word 6 never does. Wrong strides, a `>=` count compare, an 8-bit key, a different offset word and a missing fallback are each caught.
5. **Other fields (lifted).** The run colour bytes come from `layouter+0x68..0x74` times 255. The method emits nothing once `layouter+0x49c` reaches the metrics limit `u16 at metrics+0x1a`, and does nothing without a source.

The lifted function reads the guest GOT, the font descriptor at `font+0x60` and uses `sp` as scratch. The oracle supplies all three and stays UBSan-only because the lifted code addresses fixed guest addresses.

## Established: the two icon-source binds (static only)

`f_7000a322cc` binds the icon source's texture at BL `0xa325f8` and `0xa32688` (words `9400544a`, `94005426`). Decoded set-up at both: `x8 = [x19+0x3b8]`, `x0 = [x8+0x40]`, `x1 = 0`, call `0xa43940` (returns `[x0+0x30] + index*0x78`), `w2 = [x19+0xf2]` (slot byte), `x1 = result`, `x0 = x20` (render context), `w3 = 0`, then `bl 0xa47720`. That helper forwards the entry as first argument and `x0 + slot*32 + 0xe8` as second to `f_7000961224`, which reaches the BindTexture import chain. `x19` is written only by the prologue `mov x19, x0` and epilogue restores in the function body. The decoded sites are the same two on both control-flow paths, so both are wrapped. The glyph-pass binds `0xa32414`, `0xa324c4`, `0xa32540`, `0xa32770` and the `node+0x3c8` bind `0xa32568` are ordinary and are left unwrapped.

The icon source reaches the node as follows (static): `f_7000a1d260` passes `*(*(*0x1d54ba0)+0x58)+0x68` as `f_7000a27920`'s fourth argument, which caches it at record `+0x358` and gives it to `f_7000a204cc`, which stores it at `layouter+0x18`. `f_7000a28358` copies the record to `node+0x398..0x3b8`.

Whether `c->x[19]` really is the node at the wrapped call site is checked **at run time** by the probe: it recomputes entry 0 from `c->x[19]+0x3b8` and compares it with the argument the guest passes. That can only match if the register is right, and the log prints the result for every event.

## Runtime result: first run of the corrected probe (November 9, 2026)

Recording preserved, with manifest, at `/var/mnt/game_drive/github/glyph-icon-route-recording-20261109T190146Z/` (the working copies stay in `build/glyph-icon-route-20261109/`). Exit 0, 178.58 s, 37,387 bytes, no process left. Marks at game.log bytes: `start` 24970, `visible` 27294, `visible` 34715 (the owner opened the page once early, then reopened it), `end` 35492. `analysis.txt` and `uv-check.txt` hold the derived tables.

**Observed (runtime):**

- The layout hook fired for six distinct icon codes, all from **one icon source `X` = `341b36c10`** with record count **48** and scale floats 50×46: `crs` (record 0), `a` (7), `mns_l` (27), `anarp_l` (34), then, after the first `visible` mark, `x` (5) and `crslr` (22). Codes equal record indices, and the tags are `ST:icon` names from the message tables.
- The logged record words are UV rectangles on an **8×6 grid of 46×50-texel cells over 368×300**: for all six, words 2 and 3 are 45.0×49.7 texels and words 4 and 5 are the origin at `(col*46 + 0.5, row*50 + 0.5)` (for example `a`: record 7 = column 7, row 0, origin 322.5, 0.5; `crslr`: record 22 = column 6, row 2, origin 276.5, 100.5). `uv-check.txt` has all six. The role of words 2 to 5 is read from these values; the oracle only established that words 0 to 5 matter.
- Both second-pass bind sites, `0xa325f8` and `0xa32688`, ran for one node `34505d210` whose `node+0x3b8` is that same `X` (seen at layout), with **`x1 == entry 0` recomputed from `c->x[19]` at both**, so the `x19` assumption held. Draw counts: glyph pass 16, second pass 1.
- The registered texture behind that bind is texture 816, handle `10000330`, **368×300, format 77, no view**. Format 77 is the code the accepted HUD atlas path requires for the 512×192 `icon_button` image, which is BC7 offline, so it is consistent with BC7. That equivalence is an inference.
- Call totals: 1000 or more binds (below 10,000) and 10 or more but fewer than 100 layout calls over the run.

**What this establishes:** for this run, an icon laid out through `f_7000a21afc` and the texture bound in the draw's second pass come from the same 48-record source, whose texture is 368×300, matching the offline `font_icon` description, and whose records are cells of that atlas. The contradicting outcomes the probe was built to catch (another size, a source not seen at layout, `x1 != entry0`) did not occur.

**What it does not establish:**

- **The Auto-attacking page specifically.** Events are deduplicated per source/code and per site/source/entry, so repeat uses are invisible: the `a` event (byte 18130) predates the `start` mark, and the two bind events (bytes 21531 and 22025) predate it too. The bind totals show the 1000th bind call at byte 26858, before the first `visible` mark, so these binds come from text on screen from early in the run, not from the Tips page. The only icon events inside the marks are `x` and `crslr`, new codes laid out between the two `visible` marks; the log cannot say which page text they belong to.
- Which in-game text owns node `34505d210`, and whether other nodes use other sources. Only one source appeared.
- Whether a second font-icon variant (the standard and large pair offline) shares this source.
- Refresh behaviour of copied commands, and any replacement design. Nothing here is a replacement contract.

A follow-up that attributes events to a page needs a probe change (for example resetting the de-duplication when a mark is written, or counting each code per phase). That is a new diagnostic and needs review first.

## Not established after the run

- That the Gameplay Tips A icon itself goes through this path (likely, given the tag vocabulary and shared source, but unobserved for that page).
- Whether the second pass draws the layout's runs (draw count 1 with one icon-bearing string is suggestive only).
- Copied-command refresh after a style change.

## The probe (opt-in `XCX_GLYPH_ICON_PROBE=1`, read-only)

`recomp/appendage.py` adds three guarded sites; `runtime/appendage_glyph.c` holds the logging; the record lookup is in `runtime/inline_icon_probe.inc`. Generated guest C differs from the accepted output by one include, one entry hook and two bind wrappers; `recomp/patches-upd.txt` SHA-256 is unchanged.

- **Layout events** at the `0xa21afc` entry: code, key, tag name, source `X`, lookup status (including no source and no table), count, scale, selected index, record address, the seven raw record words and six floats, run tint, `layouter+0x49c`/limit, font, metrics and caller. At most 64 distinct source/code pairs.
- **Bind events** at `0xa325f8` and `0xa32688`: site, node, source `X`, whether that source was seen at layout, source status and header, texture set pointer, recomputed entry 0, the guest's `x1`, `==`/`!=`, slot byte, flag, glyph and second-pass draw counts from the geometry, and the 15 words of the entry. At most 32 distinct site/source/entry triples. Each event arms the existing handle observer, which logs the registered texture's handle, slot, serial, storage, **actual width, height and format** and view as `resource probe 200+n`. A bind that never reaches BindTexture logs that instead. Nothing is filtered on 368×300.
- **Totals** are logged at 1, 10, 100, … calls, so volume stays visible after the caps.
- **Bounds and safety:** 480 s from adapter start, the caps above, every dereference guarded by `plat_mem_readable`, no pointer kept after its call, original execution always runs unchanged with CPU state restored (fixtures compare the whole `Cpu`), and a bind scope the caller had set is restored.
- The superseded probe (vtable slot `+0x78` and glyph-pass binds) is removed, along with its fixtures.

## Checks run

| Check | Result |
| --- | --- |
| `verify_sites.py` against the pinned `main` | 33/33 pass (after correcting two over-literal patterns and a scan range of mine; no code assumption changed) |
| `tests/inline_icon_oracle.py` | 48 combinations pass; 5 deliberately wrong lookups rejected; one gap found and closed (code `0x105` separates 16-bit from 8-bit keys) |
| `tests/appendage_glyph_test.py` (ASan/UBSan) | all modes pass, including `icon` |
| Regeneration | 197/197 chunks, 0 unsupported, `patches-upd.txt` unchanged |
| Generated C diff against the accepted worktree | one include, one hook line, two bind lines |
| Native build, two jobs | 304 objects, 0 errors; probe strings present, superseded strings absent |
| `check_content.py`, `--staged`, `git diff --check` | pass |
| Owner game run | exit 0, 178.58 s; results in the section above |
| Not run | Unicorn differential suite (no lifting changed; Unicorn is not installed here) |

## Owner test (run once; commands kept for review)

Directory: `/var/mnt/game_drive/github/xcx-recomp-glyph-local/build/glyph-icon-route-20261109`. Candidate `run/xcx` SHA-256 `01af1cad43ded75f0a92a505f0895ffb276c6fbf199b41e1832604fa1e2348f5`. Profile: `/var/mnt/game_drive/xcx-prompt-test-profiles/glyph-inline-trial-20261109-03`, a verified copy of the pristine disposable profile (`profile-copy.json`).

```bash
cd /var/mnt/game_drive/github/xcx-recomp-glyph-local/build/glyph-icon-route-20261109
python3 launch.py --check      # no window
python3 launch.py              # game, 540 s limit
python3 mark.py start|visible|end   # from a second terminal, see checklist
python3 control.py dry-run     # lists the exact candidate process, if any
python3 control.py stop        # SIGTERM to that process only
python3 analyze.py             # after the run: facts by phase
```

Checklist: load slot 2, then Main Menu > System > Gameplay Tips and highlight **Auto-attacking** (do not open it yet), run `mark.py start`, open the page, and while the A icon is visibly on screen run `mark.py visible` and wait 10 seconds. Leave the page, run `mark.py end`, close the window normally. Change nothing.

The marks record `game.log` byte offsets, so the analysis can separate icons laid out and bound while the page was open from earlier text. Reading the result:

- A layout event with tag `a` between `start` and `end`, whose source reappears in bind events with `x1 == entry0` and a texture of 368×300: supports the `font_icon` hypothesis for that path.
- Any other size or format on those binds, or a source that was not seen at layout: contradicts it. Report it as found.
- No layout event in that window: the page was not laid out through `f_7000a21afc` then (or was cached from before `start`; reopen it after `start`).
- Layout events but no second-pass bind, or `x1 != entry0` everywhere: the draw-side assumptions are wrong, which is also a result.

A single run proves what happened in that run only. The launcher refuses to run again over its recording; a further run needs a new evidence directory and a reviewed reason.

## Replacement candidate (November 9, 2026)

Status: **implemented and automatically tested; not yet visually accepted by the owner.** It replaces eight ordinary buttons in text lines,
through the shared `ST:icon` source, with no change to the native atlas.

### What it replaces

The 48 records of the shared source are the msbp `icon` enum in order (A=7, B=8, X=5, Y=6, L=13, R=14, ZL=15, ZR=16; the same 24 again as
large variants at +24). Sixteen cells are replaced: those eight actions in the small and the large variant. Every other cell keeps its
native pixels: D-pad, sticks, `+`/`-` (no matching tile), power/home/capture, `anal/anar/analp`, `crs*` compounds.

| Source finding | Rows in the offline message tables (`build/glyph-inline-replace-20261109/all-icon-rows.json`) |
| --- | --- |
| 279 rows carry any `ST:icon`; 218 carry a replaceable button tag | `MNU_CtrlInformation_ms` 86 (menu bottom hints and Control Guide), `MNU_FooterGuide_ms` 56 (large), `MNU_TipsList_ms` 43, `FLD_HintList_ms` 22, `menu_program_ms` 8, `MNU_CommonTelop_ms` 2, `MNU_Option_ms` 1 |
| **Not found as text** | Settings tab L/R: no L/R row exists in the option tables. Whether those tab icons use this path at all is **unverified** (likely screen-layout resident); the owner checklist asks to look. |

Tags are traced to records through the enum and through runtime-captured tag/record pairs (not through appearance); every R tag is a
single record (R = 14, large 38), but the *action* each R stands for differs per sentence, so the replacement follows the physical
button, which is what the glyph policy already does for the HUD route.

### How it works (shared inline path)

1. `f_7000a322cc` binds entry 0 of the source's texture set at `0xa325f8` and `0xa32688` through `f_7000a47720`. The generator wraps those two
   `BL`s (`appendage_glyph_icon_bind`, instruction words verified). The wrapper checks that `x19` is a node whose `+0x3b8` is a source
   with 48 records, scales 50/46 and the SHA-256 of the 28-byte record table (`INLINE_RECORDS_SHA256`), and that `x1` is exactly entry 0
   with the 368×300 / format 0x4d descriptor. Anything else leaves the original path untouched.
2. If the current style replaces any of the eight, the BindTexture import inside that one call returns a tagged handle
   (`0xa6…`: signature id in bits 32..43, original texture/sampler ids in the low 32 bits). Same carrying/resolving contract as the accepted
   HUD handles (`0xa7…`): copied draw commands keep the handle; the resolver reads it at draw time; no guest memory or native atlas is written.
3. The resolver builds, once per (signature, atlas storage), an owned 368×300 RGBA8 image: the native BC7 atlas is decoded by
   `vkCmdBlitImage` on the GPU (no CPU decoder, no pixels redistributed), then `vkCmdCopyBufferToImage` overwrites only the replaced cells
   with the licensed tiles fitted into each cell's native glyph box. Image and view live until shutdown. Failure at any stage returns the
   native texture.
4. Policy is `glyph_decide_inline_set`, the HUD policy applied per action: Original, Automatic (native for Nintendo controllers), PlayStation,
   Xbox, Steam, Keyboard/Mouse, Standard/Colored, Button layout (Nintendo vs Xbox swaps A/B and X/Y), and the current `InputMap`.
   An action that is unbound, bound twice, shared with another action or has no tile stays native; the other seven are unaffected.
5. Refresh: a changed style, binding or layout gives a different signature at the **next** bind, i.e. the next time text is laid out and drawn.
   No rebuild is forced. Text whose draw commands the game caches keeps its handle until that page is rebuilt, so the checklist says to close
   and reopen the menu page after changing a setting.

### Limits (stated, not hidden)

- **Colored**: the tiles are drawn through the text material, which tints by the text colour. Standard tiles have white bodies and are
  expected to look right; Colored may look tinted. Unverified until seen.
- At most 24 distinct signatures per session; beyond that, new combinations stay native.
- The signature fixes the atlas storage at first capture. If the game moves/reloads the atlas (new serial or storage), the handle falls back
  to the native texture.
- The composite snapshots the native atlas when it is built, so the native glyphs beneath the replaced cells are not shown anywhere.
- Opt-out: `XCX_INLINE_GLYPH=0` disables only this path.

### Checks run (this tree, `build/glyph-inline-tests-5/`, `build/glyph-inline-oracle-4.log`)

| Check | Result |
| --- | --- |
| `tests/appendage_glyph_test.py --gpu-build build/glyph-inline-trial-5` | exit 0: policy (14,620 cases), unit enabled/guides/resources/icon/inline/disabled, hook identity, SDL, GPU stages normal/pixels/image/upload/blit/view/source |
| Inline GPU fixture (RX 7900 XTX, real Vulkan) | A synthetic 368×300 BC7 atlas of solid blocks is decoded by the real blit and compared byte for byte with the expected image: every texel outside the replaced cells equals the native decode, every replaced cell equals `glyph_art_inline_cell`, the native BC7 blocks read back unchanged, a second signature gets its own image, an atlas re-registration or foreign storage falls back to native, and every injected failure keeps the native texture |
| Lifted-code oracle (`tests/inline_icon_oracle.py`) | exit 0 against the regenerated guest C; five mutations of the record lookup rejected |
| Source identity | the closed-form record table hashes to `INLINE_RECORDS_SHA256` (checked in Python and in the unit test) |
| `check_content.py`, `--staged`, `git diff --check` | exit 0 |
| **Not covered** | Vulkan validation layer is not installed here (`validation enabled 0`), so zero reported errors says nothing about layer coverage. The fixture uses solid BC7 mode-6 blocks, not the game's atlas; real game use is the owner test. No Unicorn run (lifting unchanged). No Windows build. |

### Owner test

Candidate `build/glyph-inline-replace-20261109/run/xcx`, SHA-256 `efa566bd4cc836a41902b94cf54162f8fa963cfbda25ec52d4a0d82b3f99894b`, built from `generated/upd` with two jobs into
`build/glyph-inline-trial-5`. Profile `/var/mnt/game_drive/xcx-prompt-test-profiles/glyph-inline-replace-20261109-01` (a fresh copy of the pristine
`glyph-actionable-20261109-01`; all 46 recorded files verified). Commands, locations and the comparison checklist are in
`build/glyph-inline-replace-20261109/OWNER-CHECKLIST.md`; the launcher refuses a profile that no longer matches `profile-copy.json`.

### Owner observations and remaining limits (November 9, 2026)

Two owner sessions with the candidate. These are owner-reported screenshots, not independent traces.

- **Observed working:** inline buttons in Gameplay Tips text, a field hint pop-up (`L + right stick`), and the Map/Segments footer (`ZL` tiles, `Y`, `X`), with PlayStation and Xbox styles, Button layout changes and Standard vs Colored. Colored face tiles rendered at full colour with no visible tint on the Map footer. The `X Access` field prompt (accepted HUD route) still works.
- **Native, layout-based prompts (not covered):** tab `L`/`R` icons (Map, Tips), page-turn `R` icons, `Details ... ZR`, the popup `A Close` (Story Mission Complete, tip popups), `X Check Control Guide`, the `R + D-pad` mini-map widget. No `Close` + A message row exists, which fits layout-resident widgets; the route into their textures is not traced.
- **Native on purpose:** stick-presses (`Find Self`, `Show Navigation`), D-pad, `+`/`-`.
- **Not yet accepted:** Keyboard & Mouse and Automatic with inline text, and whether a page reopen is needed after a style change. Acceptance of the whole candidate is pending the source review.
