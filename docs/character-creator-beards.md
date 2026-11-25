# Character creator: facial hair

Implementation in progress; no working extended feature yet. No builds or runtime tests in this implementation session yet. All addresses below are offsets in the 2.0.0 main.

## Verified state and storage

`main+0x769ee0` reads character-record byte `+0x57`; at the block `0x769fdc` it extracts bits 5 and 6 into creator words `+0x714` and `+0x718`. Each native selection is boolean. Model setup at `0x698278` reads the same byte through `[this+0x740]`, then calls `0x32dd98` with bit indexes 5 and 6 (returns `0x698290`, `0x6982a0`). Save serialization and an encoding for extra donor choices still need tracing.

`0x699368` and `0x69937c` forward boolean choices through `0x697a04` and `0x697a1c` to `0x32dd98`, which changes flags at model-controller `+0x2bc` and dirties `+0x2c0` with bit 4.

## Draw components

`0x32c8a4` with third argument 5 searches higeUA..higeUD and higeDA..higeDD through `0x962d30`. Missing names return -1 and are skipped. Upper names use flag 0x20, lower names 0x40. `0x962e24` stores the selected float (0 or 1) at named runtime entry +0xa4, stride 0xb0. Its final rendering interpretation remains to be traced.

The V10040 wimdo models header has an extended-component offset/count at +112/+116, relative to the models block. Entries are 16 bytes with two relative name offsets and two words. In creator heads, the names pair higeUA with JU_head and higeDA with FC_jaw. Face 0 has neither; faces 1–2 only lower; faces 3–6 both.

Format reference consulted: ScanMountGoat/xc3_lib, xc3_lib/src/mxmd/legacy.rs, Models, Unk6, Mesh (https://github.com/ScanMountGoat/xc3_lib/blob/main/xc3_lib/src/mxmd/legacy.rs). Reference downloaded under ignored build/beard-research; no upstream code incorporated in runtime.

Initial inspection output is build/beard-research/heads.json. WARNING: its hige_meshes mapping is provisional and includes ordinary face meshes because component index zero is also used by meshes without an extended component. Decode the enabling flags before using this list to filter geometry. Names and attachments are independently decoded.

## Next experiments and constraints

Identify the enabling mesh flags; isolate a donor upper/lower component while retaining its material and stream dependencies. Test on face 0 using a separate binary and profile, then implement per-wearer selection. Do not replace the whole head or globally alter a shared donor. Storage must preserve native saves and retain independent donor selections. NPC heads need a complete survey, including facial hair incorporated into ordinary face geometry.

Use runtime/avatar_beard.c/.h and recomp/avatar_beard.py; leave the concurrent hair work untouched. Every build uses /tmp/xcx-build.lock and Fedora-Tools clang; every harness run uses /tmp/xcx-harness.lock and BIN=xcx.beard, run names prefixed b. Never kill the owner game. No commit or push.

Further inspection: mesh flags1 bit 0x100 distinguishes the facial-hair draws in fc121003: meshes 9 and 10 use material 9, vertex buffer 5, index buffers 13 and 14, extended component indexes 0 and 1. Ordinary meshes have bit 0x100 clear. heads.json now has extended_meshes filtered by this bit; validate its runtime meaning before merging.

## First attachment experiment

New runtime/avatar_beard.c/.h and recomp/avatar_beard.py register an entry hook at 0x962e24 (verified instruction 0xf9400808) via --avatar-hair-hook. XCX_BEARD_COMPONENTS=log logs named hige setters; =show forces their scalar to 1 for experiments. No extended selection feature is enabled by this hook.

Build hairB1 started under flock /tmp/xcx-build.lock, with live exec session 95136. First it waited for the concurrent hair24 build; it then began generation at 22:17:09. Do not restart without checking this process/session and hairB1-status.txt.

Isolated mod root: build/beard-research/mods (use absolute XCX_MOD_DIR). donor/romfs/chr/fc/fc112002.wimdo contains fc121003 with its draw list reduced to the two facial-hair meshes, with the extended-component bit cleared to test unconditional rendering. Its matching donor wismt is served as fc112002.wismt. All other dependencies remain intact. This temporary experiment replaces a hair, not the final feature. Run with BIN=xcx.beard, XCX_HAIR_FIT=0 initially, and a unique b-prefixed run/profile under the harness lock after the build succeeds. Inspect the screenshot before interpreting the result.

More storage accessors: 0x33e7b0 writes byte +0x57 through the wrapper pointer at +8; 0x33e7bc reads it. Need trace callers and persistence before allocating extra donor IDs.

Creator selection writes found: blocks 0x778d14 and 0x778d50 store selected grid index into UI+0x714/+0x718, then collapse nonzero to a boolean in UI+0x103 bits 5/6. UI+0xac is therefore consistent with the start of the character record (+0x57 gives +0x103). Extending the grid alone loses donor identity here; hooks must retain it in serialized storage and restore it when reopening the creator. hairB1 generation passed at 22:17:40; compilation proceeding.

## NPC and material survey

Material entries are 0x88 bytes (not the initially tried 0x84). Material name offsets are relative to the materials block, whose file offset is wimdo+12. The corrected survey in build/beard-research/heads.json has materials and facial_hair_material_draws. It found NPC-only facial-hair draw candidates: resource 3 fc181030, material hige1, mesh 8, VB3/IB11; resource 6 fc181060, material fc181060_hige, mesh 5, VB2/IB7; resource 352 fc181031, material hige1, mesh 8, VB3/IB11. These draws have flags 0x800000, not the creator 0x800100, so lack named toggle components. Resource 352 may duplicate 3 and requires comparison. Material names are evidence for donor candidates, not proof of visual distinctness or complete coverage of texture-only facial hair.

## Result 1: a merged head renders on face 0 (2026-11-20)

A head file is a wimdo (model description) plus a wismt (DRSM header copy of the wimdo's streaming block plus one xbc1/zlib stream). Stream entries: 0 vertex data (at 0), 1 shader program set, 2 low-resolution textures. The facial-hair draws are ordinary meshes whose flags1 has bit 0x100 and whose extended-component index selects an entry of the Models extended table (higeUA/JU_head, higeDA/FC_jaw); the game draws them only while the component's float is 1, which `main+0x32c8a4` sets from bits 0x20/0x40 of `this+0x2bc` (character byte +0x57 bits 5/6).

Transplanting the two draws of fc121003 into fc111002 (face 0, which has none) needs, besides the meshes: new vertex buffers (their weight-index words rebased), the donor's weight entries appended to the weight buffer that `wbi[1]` (stream +16+2) names, bone bytes remapped by bone name (name order differs per head), new index buffers, the Models index-buffer count at Models+92 raised (the game sizes per-index-buffer arrays from it; leaving it at 13 crashed in `main+0x99f500`), the vertex region grown in 0x1000 steps with the shader and low-texture entries shifted, and both streaming-block copies (wimdo and wismt header) patched. With the eyebrow material cloned for the draws, face 0 shows the donor's beard attached to the jaw bone in the creator (harness run ba3, `XCX_BEARD_COMPONENTS=show`).

## Result 2: the working feature (2026-11-21)

What the owner gets: with the mod made by `tools/beard_mod.py` installed, the Mustache and Beard tabs of the Hair page show on every male face and both choices draw something on faces 0, 1 and 2, as they already did on faces 3 to 6. Saved and loaded like any avatar choice, because the game's own flag (character byte +0x57 bits 5 and 6, `this+0x2bc` bits 0x20 and 0x40) still does the switching and nothing about the save changes.

How it is built, and what was learned on the way:

- Only the head model files change (`fc111002` face 0, `fc121001` face 1, `fc121002` face 2), as a loose RomFS mod (`<mods>/<id>/romfs/chr/fc/*`; mod ids must be lower case). Face 0 gets face 3's mustache and beard, faces 1 and 2 keep their own beard and get face 3's mustache.
- A material added to a head (a clone of the eyebrow material with its textures added to the streaming tables) crashes the game in the per-material uniform loop (`main+0x9bdc50`, a 16-byte `memcpy` to a garbage destination), even when no per-material table is extended. Extra low-resolution textures alone are harmless. So the added draws use the recipient's own eyebrow material (`mayu*`), which also follows the hair colour: the beard takes the avatar's hair colour.
- The eyebrow texture is a small alpha-masked strip. The beard's own texture coordinates happen to land on opaque parts of it, so the beard keeps them. The mustache's coordinates (a corner of face 3's beard texture) land on the transparent part and the mustache vanished, so its vertices all sample the centre of the eyebrow's mesh instead: a solid, slightly blocky mustache in the hair colour.
- The recipient's face is shaped differently from the donor's. Every added vertex moves along z by the difference of the two faces' front surface at its (x, y) (ray test against the face skin triangles, the median where one face has no skin).
- Face 0 first showed the beard but never the mustache; the cause was the transparent texture area above, not depth. A shift computed from the nearest skin vertices was wrong near the chin and buried the beard, so the ray test replaced it.
- The Mustache and Beard grids have two items each (none, one icon). Selecting an item closes the grid back to the tab list. `DEF_PcList.Beard` is not a beard type (values 70 to 140).
- The tabs: `avatar_hair_hige` (runtime/avatar_hair.c) now switches on by itself while a mod provides `chr/fc/fc111002.wimdo`; `XCX_HAIR_BEARDS=0` forces it off, `=1` on.

Making the mod: `python tools/beard_mod.py --romfs build/extracted/upd/romfs --out <instance>/mods/beards`, then enable the mod (F1 menu or `romfs-enabled.txt`). Other donors: `--mustache-donor fc131001|fc131002|fc131003` and `--beard-donor` any creator face with facial hair or the NPC beards `fc181030` (whole-face beard with mustache) and `fc181060` (goatee). `--extra-z` pushes the added vertices forward, for debugging. Checked in the harness: face 0 (final tool output, tabs switched on by the mod alone), face 1 and face 2 with face 3's mustache, and face 0 with the NPC beard `fc181030` (a solid whole-face beard with sideburns; its own texture coordinates only left a thin outline, so NPC beards use the flat coordinate like the mustache). Other creator donors than `fc121003` are untested (their beard texture coordinates may also fall on the transparent part).

Checked faces and options, as seen in the creator (profile view): face 0 mustache and beard show in the hair colour; face 1 mustache shows (dark hair), native goatee unchanged; face 2 mustache shows only faintly (the hair is light grey, so the band is nearly invisible); faces 3 to 6 are native. Things to look at: the mustache is a plain solid band, the cheek part of face 3's beard looks wide on face 0, and nothing was tried outside the creator (field, cutscenes) or after saving and loading.

Not done: a choice among several mustache and beard types inside the game (every added draw would need its own component, a way to store the type in the avatar record, which has no spare field we know of, and longer Mustache and Beard grids), and a texture of its own for the added draws (blocked by the crash above). Face 3 to 6 are unchanged.
