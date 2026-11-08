# Experimental Appendage input glyph styles

This adapter changes the three button images in the enemy-targeting **Appendage** guide for the verified XCX DE 2.0.0 build. Original rendering is the default. Players choose the style under F1/Esc → Controls; see [Controls](controls.md#experimental-appendage-glyphs) for use and fallback behavior. Other HUD, menu, tutorial and QTE prompts are outside this adapter.

This page is the developer reference. [Review evidence and checklist](input-glyph-review.md) separates source contracts, automated checks and Linux owner acceptance. Native Windows and Vulkan validation-layer acceptance remain unverified. Appearance variants are deferred.

## Supported build and insertion points

| Identity | Value |
| --- | --- |
| Main NSO SHA-256 | `0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026` |
| Main build ID | `ec4ed46bbc4a9ebbe7ce0af7a4a8ebbc09e5724a` |
| Main module base | `0x7000000000` |
| Component | `hud_PartSelection01` / Appendage, `mc_Top/mc_Guide` |
| Nodes | Three type-1 `rect_Btn` nodes, verified vtable `main+0x1c94318` |
| Resource | Verified `icon_button.wilay` prefix and selected-record hashes |

`recomp/appendage.py` verifies the source hash, build ID, module base and all six instruction words before emitting adapters. Unsupported build IDs receive no adapters; identity/instruction mismatch for the supported build stops generation. Controls is disabled for an unsupported baked game identity.

| Main offset | Adapter |
| --- | --- |
| `0x60f68c`, `0x60f6a8`, `0x60f6c4` | Observe the actual resolved R/Y/A wrappers before the original selector calls |
| `0xa27678` | Scope the selected live node while preserving the original bind call |
| `0xa2fac0`, `0xa2fcd0` | Retire the node before destruction or initialization |

The runtime also checks the live node type/vtable, 24-record count, resource prefix, record bounds and selected-record hashes. Selectors 14/6/7 identify R/Y/A artwork, not host action IDs. All three verified nodes must be live before replacement is enabled. Generated guest C is never edited by hand.

## Actions, bindings and display policy

The Appendage chord is virtual R held with Y or A for previous/next appendage. The inspected input dispatcher connects the R shift state with Func66/Y and Func67/A on the controlled actor. Artwork lookup is separate from action consumption and physical binding translation.

`input_glyphs.c` translates `B_R`, `B_Y`, `B_A` through a copied current InputMap at each eligible draw bind. Each action needs exactly one supported binding with no other action sharing that physical input. Multiple, shared, unbound or unsupported mappings retain original virtual Switch labels for the entire chord and explain the reason in Controls. Original fallback is a truthful virtual label, not a claim that the host button has that lettering.

Manual styles change artwork only. They do not change physical mappings, emitted input or the existing rebinding editor. Standard face/shoulder/trigger buttons are supported for PlayStation, Xbox and Steam Controller artwork. Keyboard/Mouse supports current unshifted letters, digits and left/right mouse; unsupported labels fall back. Steam styling covers exposed standard controls, not arbitrary paddles or touch regions.

Automatic uses copied physical activity before keyboard/pad merge and scripted input. It observes fresh keys/buttons, sustained strong stick/trigger intent and sustained captured camera motion. Held inputs, small drift, reconnect/focus baselines, incidental motion and single spikes do not repeatedly steal selection. These thresholds do not change gameplay dead zones. SDL touch/pen-generated mouse events are excluded from intent without changing gameplay handling. Arbitrary unmarked OS injection cannot be reliably identified.

Family detection uses SDL's exposed gamepad type. Steam Input presenting an Xbox interface is treated as Xbox; hidden hardware is not inferred. Switch, unknown family and no intentional source use original labels. Intent remains tracked in manual mode so returning to Automatic uses the last intentional source.

The preference `glyph_style = original|auto|playstation|xbox|steam|keyboard` is persisted independently from `pad_layout` and `bind_*`. No Lua loader/provider or external API is required.

## Draw selection, refresh and ownership

`appendage_glyph.c` scopes only an eligible live node's original draw/bind call, the same Cpu and the verified imported BindTexture return. It tags only the copied immediate texture handle. The original texture/sampler low 32 bits remain intact; the upper tag carries immutable generation and artwork IDs. No guest resource, command memory, active-bank field or global atlas is modified.

The selected record resolves through resource texture table[0], GetTextureHandle and BindTexture to the host texture. Source registration serial/storage identity is captured under the texture lock and checked again at replay. Those scalar equality guards never dereference retired storage. A reused texture ID cannot silently adopt a different registered source. Shader-type, feedback, freshness and descriptor resolution retain full-handle identity in their relevant lookups/memos.

Each artwork owns an immutable 512×192 RGBA image outside guest texture slots and alias caches. Its licensed 64×64 tile occupies the three verified UV regions; the remaining pixels are transparent. Original geometry, UVs, clipping, alpha, animation, material/sampler and draw order are preserved. There is no additional draw or covering overlay.

Pixels, image allocation and mapped staging upload are checked. Upload uses the existing upload batch and deferred staging retirement. Views use existing view caching. Private images and CPU pixels remain owned until renderer shutdown, independently of guest-node destruction. Shutdown waits for upload/GPU completion before retiring views/images and freeing pixels. Pixel/image/upload/view/source failures retain original rendering. Existing renderer-wide fatal infrastructure failures and device-loss recovery are not newly solved. At most 62 images are created on demand; each has one upload attempt per renderer lifetime.

Style/binding/source changes take effect when the guide records its next original eligible bind. Existing recorded commands retain their copied selection. No guest dirty setter is invoked from an arbitrary thread, no menu is repeatedly rebuilt, and no borrowed node/resource pointer is cached after retirement. Refresh of indefinitely cached commands outside this verified guide is not supported. Natural guide timeout and reappearance on retargeting remain vanilla behavior.

### Generation-history limit

There are **192 successful button-node attachment records per process**, allowing at most 64 complete three-node constructions. This is not a frame, elapsed-time or enemy-target count. Reused live nodes consume no new records; reconstruction does. Partial constructions can consume records without producing a complete visible guide. Attachments consume the same budget even while Original is selected, unless the adapter is disabled with `XCX_APPENDAGE_GLYPH=0`.

The final complete trio can keep rendering after it fills the budget. Once retirement or another construction leaves that trio incomplete, new attachments cannot be registered and newly recorded binds retain original labels for the rest of that process. Controls reports **glyph history exhausted; restart game to restore styles**. Changing the style, reconnecting or retargeting does not reset the history. Copied commands remain valid through their existing resource checks; records are never recycled while commands may reference them.

This remains a visible experimental limitation for review; unlimited-session style support is not claimed. Removing it would need a separately reviewed command-lifetime/retirement design. It is intentionally unchanged in this feature.

## Licensed assets and reproduction

Source: Kenney Input Prompts **1.5A**, CC0. The 62 curated style PNGs, exact archive member names and per-file SHA-256 values are in [styles/provenance.json](../assets/appendage-glyph/styles/provenance.json). The original [license](../assets/appendage-glyph/License.txt) and [third-party notice](../THIRD_PARTY_NOTICES.md) accompany them. A file-specific Git attribute preserves the upstream license bytes, including line endings and whitespace, so its provenance hash survives checkout. The older R1 PNG/include/tool remain an attributed reproducibility fixture, not the production multi-style asset path. No Nintendo resource pixels are distributed.

| Artifact | SHA-256 |
| --- | --- |
| Original Kenney package | `ac2fcf599080b0f3ba2d174c9474db6df1a0e96ff0662580e2da79a122ab78a1` |
| License | `ca2f58f80f903512119da5828868199478119818cd7ecffb303d625cb74cbb5a` |
| Styles provenance | `ea32b044071b39d0c5cabc51b1471a8f28a681402df0e257d3b256d7c99d4576` |
| Production licensed derivative | `4bafb6e18f8c4abcac52d167943886da48bdd6cc75b1800c1f73738e77d80d94` |

Reproduction needs Python 3.14 with `compression.zstd` and Pillow (verified with Pillow 12.3.0). From the repository root:

```sh
python3 tools/make_glyph_styles.py --out build/glyph-repro/styles.inc
cmp build/glyph-repro/styles.inc runtime/glyph_art.inc
python3 tools/make_appendage_glyph.py --out build/glyph-repro/r1.inc
cmp build/glyph-repro/r1.inc runtime/appendage_r1.inc
```

The production include contains 62 individually compressed licensed tiles, totaling 17,661 compressed bytes. The tool verifies source hashes and dimensions before writing the derivative. Reproduction does not read game assets or require a GPU.

## Build and checks

Use the toolchains and dependencies in [Building](building.md). This adapter is for `upd` / verified XCX 2.0.0; do not substitute the older versions named in general build examples. Runtime `.c` files are discovered by the existing build tool. A runtime-only change requires a native rebuild, not regeneration. Generator/site changes require regeneration with the pinned ExeFS and preserved patch file, then native rebuild and relevant differential checks. Asset changes require licensed-derivative regeneration and asset checks. No extraction is needed when you already have the verified inputs.

Example from the repository root, using your own verified input paths:

```sh
python3 recomp/gen.py --exefs /absolute/verified/exefs --out generated/upd --patches recomp/patches-upd.txt --jobs 2
python3 tools/build_game.py --version upd --gen generated/upd --out build/glyph-styles --exefs /absolute/verified/exefs --romfs /absolute/verified/romfs --expected tools/data/expected-xcx-2.0.0.json --profile /absolute/disposable/profile --jobs 2
```

Do not overwrite `patches-upd.txt` or edit emitted guest C. Existing generated/native outputs and all game-derived files remain ignored. Windows source support is present, but this feature's native Windows build/runtime is unverified.

Linux fixture prerequisites: Clang with ASan/UBSan, libzstd, Python 3.14 and Pillow. Use a new output directory to preserve earlier evidence:

```sh
python3 tests/appendage_glyph_test.py --out build/glyph-tests-review
python3 tools/check_content.py
python3 tools/check_content.py --staged
git diff --check
```

The default fixture run performs no game/GPU launch. It covers policy/action/current bindings, intentional input, licensed assets, hook guards and enabled/disabled retirement/address-reuse/exhaustion. Its mutex/input/hash services are explicit test seams; it is not a scheduler race test. For generator changes, also run the relevant [Unicorn differential checks](building.md#checks); hook fixtures do not replace them.

After coordinating GPU use, `python3 tests/appendage_glyph_test.py --out build/glyph-tests-gpu --gpu-build build/glyph-styles` links the isolated build's objects and tests real upload/readback, SDL filtering, source/view fallback, slot reuse and shutdown. Check that the native objects are current first. These are Linux POSIX fixtures; Windows acceptance is separate. The native `build/glyph-styles/xcx --selftest` needs no game files or GPU/window.

Diagnostic-only controls: `XCX_APPENDAGE_GLYPH=0` disables the adapter; `XCX_APPENDAGE_GLYPH_CONTROL=/absolute/file` provides the bounded comparison gate; `XCX_APPENDAGE_GLYPH_FAIL=pixels|image|upload|view|source` exercises fallback. They are not additional user preferences. Transition logs are bounded and can exhaust before a later style change; absence of a later log is not evidence of missing rendering.

## Deferred appearance task

Colored face-button members exist for PlayStation, Xbox and Steam Controller in the pinned package. Recommend a future separate **Glyph appearance: Standard / Colored face buttons** preference, keeping family, appearance and physical bindings independent. Automatic would honor an applicable chosen appearance; buttons/families without that variant would keep their standard asset. Keyboard/mouse colors are not promised. Outline and PlayStation shoulder-shape alternatives are separate choices. No appearance variants or preference are implemented here.
