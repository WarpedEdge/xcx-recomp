# Character creator: hairstyles

How the game's character creator builds its hairstyle list, what stores the choice, and what that means for offering more hairstyles. It is the base for making every hairstyle in the game choosable ([game-data-tables.md](game-data-tables.md) explains how the tables were read).

Status: the tables and the code were read for the 2.0.0 `main`. A test hook exists (uncommitted, for the 2.0.0 `main` only) and was checked in the game on Linux with scripted input and by the owner; see the tests below. Nothing was run on Windows. Items marked "not checked" were not tested.

## Overview

- The creator is one screen. New Game and the field's avatar editing look like the same screen, and the code has one creator class, built at `main+0x767eb0`. The field version was read from code only.
- Hairstyles are rows of one table, `ITM_AvatarHairList` (141 rows). The creator shows the rows whose `AvtChrID` equals the current base face. It looks at nothing else.
- A preset is a bundle of face, hair, colours and voice taken from a character row. It does not own hairstyles.
- The choice is kept as the hairstyle's `ResourceID`, not as a position in the list.

## Tables

All of these are in the 704-table data file ([game-data-tables.md](game-data-tables.md)).

| Table | Rows | Use |
| --- | --- | --- |
| `ITM_AvatarHairList` | 141 | One row per hairstyle: `ResourceID`, `AppResourceID`, `AvtChrID`, `IconID`, `DefSet`, `AppDefault` and a name and caption id |
| `ITM_AvatarPresetList` | 28 | 14 male, 14 female. `DEFPCID` points to a `DEF_PcList` row, `SelectID` groups presets that share a face |
| `MNU_AvatarCharList` | 45 | Base faces. `ResourceID` is the face id. Rows 0 to 6 are male faces (ids 58 to 64), rows 7 to 13 female faces (ids 66, 67, 65, 69, 68, 70, 71) |
| `MNU_AvrIconHairList` | 128 | Hair icons: `AvtChrID`, `Sex`, `IconNo`. A hair row's `IconID` is a row of this table |
| `MNU_AvrIconHairExList` | 4 | Icons for the second hair list ("special" in the hair tab): two per gender |
| `DEF_PcList`, `DEF_NpcList` | 257, 735 | Character rows with `FaceID`, `HairBaseID`, `HairOptionID` and more. The creator's presets are `DEF_PcList` rows 109 to 136 |

`ResourceID` is the hairstyle's id in the game's model resources. `AppResourceID` is always `ResourceID` plus 1 for creator hairstyles and is the optional extra part (`HairOptionID` in the character rows).

## What decides the list

`main+0x7799b0` builds the list. When the face changes it walks every row of `ITM_AvatarHairList` and keeps the rows whose `AvtChrID` equals the face. The list is a vector of row ids at object `+0xbe8`, and the face it was built for is at `+0xbe0`. A position in the list gives the icon through `IconID`.

The other readers do the same compare and then look for the current `ResourceID` to find which icon is selected: `0x769ee0`, `0x770e70`, `0x777fc0`, `0x778b90`, `0x76dd90`.

The helper functions, all `main+`: `0x5466b4` ResourceID, `0x546700` AppResourceID, `0x54674c` AvtChrID, `0x546798` IconID, `0x5467e4` row count, `0x54680c` first row id. `0x768e50` builds the preset list of one gender (it reads the preset, then the character row). `0x5464a0` finds the base face of a preset.

## Lists per face

The face number is `AvtChrID`. Rows are rows of `ITM_AvatarHairList`.

| Face | Gender | Rows | Hairstyles |
| --- | --- | --- | --- |
| 0 | male | 1 to 8, 134 | 9 |
| 1 | male | 9 to 16, 135 | 9 |
| 2 | male | 17 to 24, 136 | 9 |
| 3 | male | 25 to 32 | 8 |
| 4 | male | 33 to 40 | 8 |
| 5 | male | 41 to 48 | 8 |
| 6 | male | 49 to 56 | 8 |
| 7 | female | 57 to 64, 138, 140 | 10 |
| 8 | female | 65 to 72, 139 | 9 |
| 9 | female | 73 to 80, 137 | 9 |
| 10 | female | 81 to 88, 133, 141 | 10 |
| 11 | female | 89 to 97 | 9 |
| 12 | female | 98 to 105 | 8 |
| 13 | female | 106 to 113 | 8 |

- 59 male and 63 female hairstyles, 122 in all. No hairstyle is on two faces.
- Each face owns a block of 20 `ResourceID`s (male faces from 72 in steps of 20, female from 212). The hairs look authored per face.
- Rows 133 to 141 are extra rows after the main run, for faces 0, 1, 2, 7 (two), 8, 9 and 10 (two). They show last in the grid.
- Two presets share each face, so a preset's list is its face's list.
- All 122 rows have a name, a caption and an icon.

## Hairstyles the creator never lists

22 hairstyles are not choosable.

- 19 are rows 114 to 132 (`ResourceID` 20, 22, ..., 56). Their `AvtChrID` is 14 to 32, the story characters' faces (character rows 14 to 32, face ids 1 to 19). The creator only asks for faces 0 to 13. They have no name, no caption, no icon (`IconID` 0) and no option part.
- 3 are `ResourceID` 354, 356 and 358. They belong to three characters added in this edition (face ids 353, 355 and 357) and have no row in the hair table at all.

## Fit between hairstyle, face and gender

The data shows no case of a hairstyle on another face or on the other gender. Of 89 face and hair pairs used by `DEF_PcList` and `DEF_NpcList`, every creator hairstyle is paired with its own face. The exceptions are the three added characters above. Two `DEF_PcList` rows (77 and 142) have a female flag with a male face and hair, and rows 99 to 105 have a male flag with female faces and hairs. Face and hair match in those rows, so they only show the flag is unreliable.

How a hairstyle looks on another head is not known. It has to be tried in the game.

## What the player sees

Checked on Linux, first male preset, scripted input:

- The creator opens on a grid of the 14 presets of the chosen gender. Moving the cursor applies the preset to the model at once.
- After a preset is confirmed, the tabs are, in order: size, skin, hair, eyes, makeup, voice, confirm. L and R move between them.
- The hair tab lists style, a special choice, roots and tips colours. The style list is a 4 column grid. Moving over an icon previews the hairstyle on the model.
- For the first male preset the grid had 9 icons (face 0, rows 1 to 8 and 134). The check mark was on the second icon, which is `ResourceID` 72, the preset's hairstyle.
- X opens an overview page of every setting, with the gender as its first row. MINUS opens a small menu to save or load avatar data. Female presets were not reached.

## Where the choice is stored

- The save mount has four files: `systemdata`, `gamedata`, `avatarmakedata` and `socialdata` (`main+0x340dd0`).
- The character data of the player is written with named fields, among them `FaceID`, `HairBaseID`, `HairOptionID`, `Color%d`, `TexA_*`, `Voice` and the levels (`main+0x344080`). The hairstyle is its `ResourceID`.
- `avatarmakedata` is written from the creator's save menu, which offers five avatar slots (the load menu lists five). The file was 740 bytes (0x2e4, which is 5 x 148) after one save. Its bytes are masked: XOR of the byte at `i` with the byte at `i + 0x200` shows a readable record. In it the face id 58 and the hair id sit next to each other as 16-bit values (offset 0x58 of that XOR), followed by the option id (0), as in the `DEF_PcList` columns. The hair id was 72 for the first male preset and 20 after the NPC-only hairstyle was chosen and saved. Floats of 1.0 and several 0x32 bytes follow. The mask itself is not worked out, and neither is which slot a record belongs to.
- The online avatar record has a field called `Binary` (Seamless note 09). It is very likely this data. Not checked.
- Not checked: what a build without the hooks does with a `ResourceID` that is not in the face's list when the creator opens. The loop at `0x770e70` searches by `ResourceID`, and what it does with no match was not read. The model itself should load, as NPCs use the same ids.

## Making more hairstyles choosable

Constraints from the task: the creator's own screen, list, cursor, sounds and icons; existing presets and hairstyles keep their order; old saves load as before; nothing from the game is shipped; hooks for the 2.0.0 `main` only.

1. Extend `ITM_AvatarHairList` in memory after it loads. Append copies of rows (same `ResourceID`, `IconID` and so on, another `AvtChrID`) at the end of the table, then point the table lookup (`0x3dd6e0`) at the new copy. Existing rows keep their order, every reader sees the new rows through the same accessors, and the rows come from the game's own table at run time. The row block and the string pool are one block, so the copy must be rebuilt, not grown. Duplicate `ResourceID`s need a check in every loop that looks a row up by `ResourceID`.
2. Hook each `AvtChrID` compare. About six sites, in the readers above. The list and the loops that find the current icon must stay in step, so this is more fragile. Mid-function hooks may only read and write memory ([ui-widgets.md](ui-widgets.md), "Hook rules", on the save slot pages branch).

Recommended: 1.

First tries, one hairstyle each:

- Same gender, another face: `ResourceID` 72 (row 2) on face 1.
- Other gender: `ResourceID` 232 (row 58) on male face 0. Female preset 15 already wears it.
- NPC-only: `ResourceID` 20 (row 114). It has no icon, so the first version needs a placeholder icon, and nothing for its name (the grid shows icons only).
- 354, 356 and 358 have no row to copy, so they need a made-up row. Leave them for last.

## First test

Checked on Linux with scripted input. A test hook (`recomp/avatar_hair.py`, `runtime/avatar_hair.c`, built with `gen.py --avatar-hair-hook`) adds rows from `XCX_HAIR_EXTRA`, a list of `face:row` pairs. It sits on three sites of the 2.0.0 `main`: the entry of the list builder (`0x7799b0`, to note the face), the entry of the `AvtChrID` reader (`0x54674c`, which answers with the face for an extra row) and the instruction after the builder's icon answer (`0x779a4c`, where an icon id of 0 becomes the first hair icon). Every other reader of the table compares against the same face, so one answer covers them. No table is copied.

Run with rows 25, 58 and 114 added to face 0 (the first male preset's face):

- The list grew from 9 to 12 icons: rows 1 to 8, the three extras, then row 134. The face's own rows keep their order. Extras sit by row number, so an extra with a low row number would come before rows of the face that have higher numbers. Appended rows (option 1 above) avoid that.
- Row 25 (`ResourceID` 132, the first hairstyle of face 3, male): looks natural on face 0.
- Row 58 (`ResourceID` 232, a female hairstyle with twin tails and a clip): it loads, but bare scalp shows on top of the male head. It does not fit.
- Row 114 (`ResourceID` 20, an NPC-only hairstyle): loads as short black hair and looks natural. Its icon is the placeholder. The hair did not take the creator's colour in the preview (not examined further).
- Choosing row 114 moves the check mark to it. After the creator's save menu wrote the avatar data, the file held face 58 and hair 20.
- The binary without the hooks, with that profile: loading the saved avatar from the creator's save menu showed the short black hair on the model, without an error. What the hair tab's grid shows in that case was not observed.
- The icons, the live preview and the check mark are the game's own screen. The head fades out and in while a model switches, as it does for the game's own hairstyles.

Not tested: a female face with a male hairstyle, the other presets, colour and option parts on the added hairstyles, loading a game after finishing the creator, the field's avatar editing, and Windows.

## Second test: owner recordings

The owner ran the same hook build with extras `0:25,0:58,0:114,7:2,7:33,7:114` on the first male and the first female preset and recorded both.

- Male, 12 icons. The extras sit between the face's own hairs and its last row. Hair of another male face (row 25) is not natural: the hair sits too high and makes the forehead look tall, and from the side the scalp pokes through the hair where it is too small. The female hair (row 58) shows bare scalp. The NPC hair (row 114) was black while the preset's colour was brown, so the creator's hair colour is not applied to it.
- Female, 13 icons. Extras are sorted by row number among the face's own rows (2, 33, 57 to 64, 114, 138, 140), so icons 1 and 2 are the two male hairs, the preset's own hair (row 58) is icon 4 and the NPC hair is icon 11. The male hairs on the female head show bare scalp and an odd head shape from the side. The NPC hair shows shaved sides and bare scalp on the head too.
- One garbled green frame at the start of the female recording, gone after about a second. Not seen again.
- Conclusion: a hairstyle is made for the head of its own face. Moving a hair to another head needs its size and position changed, and one scale may not suit every head.

## Fit research (read from data and code, nothing changed yet)

- `MNU_AvatarCharList` has per-face body numbers: `ScaleMin` and `ScaleMax` (92 to 106 for most male faces, 89 to 106 for female ones) and the six `BustScale` columns (zero for male faces). `HigeType` and `AgeType` tell the facial hair and age type. Nothing in the table says anything about a head or a hair.
- Every face row uses the same `ActionFile` (`pc/pc900000`) and `MotionFile` (`pc/pc010000`). Strings in the program also name `/chr/pc/pc010000m.chr` and `/chr/pc/pc010000f.chr`.
- `RSC_PCFcList` (375 rows) lists the face resources (`fc...`) with lens ranges for the eye texture. It has no hair data.
- The program names the parts of a player model `Pc%02dHead`, `Pc%02dHairBase`, `Pc%02dHairOpt`, `Pc%02dBody`, `Pc%02dRig` and others. One start-up function (`f_70001e4710`, a loop of four) registers these names with bit flags. It was not read further.
- The program loads `/hkc/pc/pc0101%02d.sar` from two functions (`f_7000328278`, `f_700032b844`), which looks like the hair and cloth physics. The strings `hair1` to `hair24`, `resel_hair`, `sidehair` and `newrim_Hair_ShadowInf` are in the program too. What they name was not checked.
- No head collider or fit calculation was found in the tables. Not checked: whether the model files carry one.

### Model files: faces and hairs are the same kind of resource

A face id and a hair id are both rows of `RSC_PCFcList` (375 rows, row id = the id, base 1). Its `Resource` column is the model name, and the files are `/chr/fc/<name>.wimdo`, `.wismt` and `<name>_rig.skl` in the archive (paths are found by hashing, see [game-data-tables.md](game-data-tables.md); all 306 `fc` models were listed this way).

A name is `fc` plus six digits: `[sex][group][kind][style][0][variant]`.

- sex: 1 male, 2 female (3, 4 other characters, 5 and 6 are extra male and female hairs, 18 and 28 are the story characters' heads and hairs).
- group: the head family. Male faces: face 0 is group 1, faces 1 to 3 group 2, faces 4 to 6 group 3. Female: faces 7, 8, 9 group 21, faces 10 and 11 group 22, faces 12 and 13 group 23.
- kind: 1 head, 2 hair, 3 the hair's option part (`AppResourceID`).
- style: 0 to 7 are the eight standard hairstyles, plus 8 on two female faces.
- variant: 1 to 3, the face within its group. The head of face 0 (`fc111002`) is variant 2, and so on.

Consequences from the names (the first reading, corrected later):

- Every face has file names for styles 0 to 7, but the designs are not the same on every face of a gender. The icon of a style number differs between faces (face 6's first style is a mohawk that faces 4 and 5 do not have). So every face's list is its own set of hairs, and no two faces in a gender offer the same eight.
- My first test put row 25 (face 3's first style, group 2) onto face 0 (group 1). That mismatch was a bad choice of row, not a fit problem.
- Group 1 has one variant per style, groups 2 and 3 have three. A style a face lacks can use another variant of its group.
- Extras that exist: male `fc512001`, `fc522001`, `fc522002` (faces 0 to 2 only, no file for group 3). Female `fc612001` to `fc612003` and `fc612102` (group 21), `fc622002` (face 10), `fc222801` and `fc222802` (style 8, faces 10 and 11). Faces without an extra have no file made for their head.
- The story characters' hairs (rows 114 to 132, for example `fc182010`) exist only for their own heads (groups 18 and 28).
- All 41 head models have almost the same box size, about 0.38 x 0.46 x 0.40 around a centre height of 1.65, male, female and story characters alike (read from the first floats after the models offset in the `.wimdo`). So a hair does not need a different overall size for another head. The mismatch is in the shape of the scalp and the position of the hair bones, which the box does not show.
- Both the head and the hair model have their own rig file (`_rig.skl`) with hair physics joints (`DJ_hair...`). A hair is skinned to its own rig, so where a hair sits on another head depends on how the two rigs' bones line up. Not read yet.

Next: find where the game places a hair part on the head, then add a test-only way to change its size and position while the game runs.

## Third test: extras that belong to the right gender

The test hook now takes a face letter in `XCX_HAIR_EXTRA` (`m` for the male faces 0 to 6, `f` for the female faces 7 to 13, `a` for all) and logs the face each list is built for. The list builder runs when the hair tab opens, not when the cursor moves over presets. The presets map to faces in this order (`ITM_AvatarPresetList` row, then `DEF_PcList` face): males 0 and 1 face 0, 2 and 3 face 1, 4 and 5 face 2, 6 and 7 face 3, 8 and 9 face 4, 10 and 11 face 6, 12 and 13 face 5; females 14 and 15 face 7, 16 and 17 face 9, 18 and 19 face 8, 20 and 21 face 10, 22 and 23 face 11, 24 and 25 face 12, 26 and 27 face 13. The grid of presets is 6 wide.

Checked on Linux with scripted input and screenshots (three-quarter view in the hair tab only; the back and side were not looked at):

- Male extras (rows 134 to 136, `fc512001`, `fc522001`, `fc522002`) added to face 0, face 3 and face 6, one face of each head group: all look natural. No bare scalp, normal hairline, the creator's hair colour applies. Face 6 is in group 3, which has no file of its own for these.
- Female extras (rows 133, 137 to 141) added to face 12 (group 23): long hair, a bob with a clip, bangs, waves. All look natural and take the creator's colour.
- So within a gender, a hair made for one head group fits another group's head. The earlier failure was my mapping (row 25 is a style face 0 already had as its own file), not a fit problem.
- Story characters' male hairs (rows 114 to 123) added to face 0: most fit well (short black, long black, blond shaggy, dark swept, blond long), but they keep their own colour and ignore the creator's. Two of the ten showed a bald head and one showed bare scalp at the temples. Which rows these were was not tracked.
- The creator's list needs no table copy for any of this: the extras use the face's own icons. Only the story characters' hairs have no icon (placeholder needed).
- The model view can come up with no tabs after the preset is chosen. It happens in the build without the hook too (checked), so it is not caused by it. B opens a yes or no prompt about going back to the preset step, and either answer brings the tabs back.

Not yet tested: the other faces of each group, the back and side views, the female story-character hairs, any hair of the other gender, saving and loading (the first three of these are covered by the fourth test).

## Fourth test: every extra, one by one

Each hairstyle was put on one face and the cursor was moved onto it, in the three-quarter view, with a screenshot per hair. The run flow is the one under "Testing by script" below, with the preset grid at its start (female presets are local indexes 0 to 13, six per row, so face 12 is one row down and four to the right). A list of 13 or fewer icons fits in four rows and the cursor wraps around at the edges of the grid.

**Female hair on a male head (face 0), rows 57 to 64, 81, 97, 133, 137 to 141:** the shape of the hair is right, but a bare patch of scalp shows on top of the head in almost every case. The hair is a little too small or too low for the male head. Only the short swept style of row 57 showed no patch. This is the same patch seen in the owner's recordings, so a hair of the other gender does not fit without a change of its size or position, which is not found yet (see the fit research above). Not tried: male hair on a female head (the owner's recording showed bare scalp there too).

**Story characters' male hairs (rows 114 to 123) on face 0:** the model is the story character's own, so it keeps its colour and does not take the creator's colour.

| Row | Look on a creator head |
| --- | --- |
| 114 | short black, fits |
| 115 | long blue, a strip of bare scalp at the top |
| 116 | long black, fits |
| 117 | short black, fits |
| 118 | short grey, fits |
| 119 | short blond, fits |
| 120 | head shows bald (a thin black strip at the back only) |
| 121 | shaggy blond, fits |
| 122 | head shows bald |
| 123 | swept blond, fits |

The bald ones are probably hairs made to be worn with the character's own head model, which may carry part of the hair (not checked). Rows 115, 120 and 122 are left out of the built-in list.

**Story characters' female hairs (rows 124 to 132) on face 12:** all nine fit, with no bare scalp: long silver, black bob, blond bob with a hair clip, silver bob, cream bob with a fringe, short red, long black, orange bob with a fringe, and one long pale blue style with small lights (row 132). They keep their own colours.

**Rows 81 and 97 (the style that only groups 21 and 22 list) on face 7 (group 21) and face 12 (group 23):** both fit, black or pink with a fringe and ends that reach the shoulder, no bare scalp.

**Why the other gender's hair sits too low (read from the rig files, one change tried).** A rig file (`_rig.skl`, header `BC` then `SKEL`) holds 153 bones for a head or a hair. The header has three tables: 48-byte local transforms at 0x120 (position, rotation quaternion, scale), parent indices (16-bit, -1 for the root) at 0x2760, and name offsets (16-byte entries) at 0x1dd0. Bone world positions follow by walking the parents. The head joint (`JU_head`) is at the same place in every male head and hair rig, and at another place in every female one:

| | x | y | z |
| --- | --- | --- | --- |
| male rig | 0 | 1.608 | -0.034 |
| female rig | 0 | 1.643 | -0.024 |

So on the male skeleton the head joint is 3.5 cm lower and 1 cm further back, while all head meshes sit at the same height (the boxes read earlier). A hair is skinned relative to the joint, so a female hair on a male head lands 3.5 cm lower than it is made for, which is the bare patch on top. The same offset reversed would explain the male hair on a female head. A fix therefore looks like a shift of the hair by the joint difference (up 3.5 cm and 1 cm forward for female hair on a male head, the reverse for male hair on a female head), with every head of a gender sharing the same offset. Not confirmed in the game: a loose RomFS mod (`runtime/romfs_mods.c`) that changed the joint's local position in a female hair's rig made no difference, even at 25 cm, and the log showed the archive never asked for those rig files by path, so the loose files are not read for hair models. Where the game reads a hair's rig and model is not found yet. The next step is to find that read, or to change the loaded data in memory.

**The built-in list (first version).** A list used when `XCX_HAIR_EXTRA` is not set, giving every face the extras of its gender and the story characters' hairs. Replaced by the full lists of the fifth section.

## Fifth test: full lists, order, scrolling, beards

Checked on Linux with scripted input and by the owner in the game.

**Every preset offers every hair of its gender.** Because the designs differ per face (see the correction above), the default list is now every male row (1 to 56) on every male face and every female row (57 to 113) on every female face, plus the bonus rows (male 134 to 136, female 133 and 137 to 141). A face's own rows come first in their natural order, then the other faces' rows, then the bonus rows. This is built in `build_default` in `runtime/avatar_hair.c`; rows are listed through the same getters as before, so the icons are the game's own. The owner confirmed the order and that the mohawk now shows on the younger presets.

**The grid's order.** The grid does not use the list of row ids at `+0xbe8` for its order. It has its own item array (12-byte entries: kind, row, flags) on the grid object at `UI+0x3f38`, built in row order, and it starts the cursor at the current hairstyle's natural position. A hook after `main+0x76de70` (`avatar_hair_items`) reorders the array (story rows go last) and returns the remapped cursor position.

**Scrolling.** After a hair below the first page was chosen, leaving the tab and coming back put the scroll bar at the top while the selection stayed. The widget call after `0x76de70` is `f_70005ac570(grid, index, flag)` and the game passes flag 0 from several places; flag 1 scrolls the index into view. An entry hook on `main+0x5ac570` (`avatar_hair_setcursor`) turns a flag of 0 into 1 for the creator's own grid. The owner confirmed it keeps its place.

**Story characters' solid-colour icons are gone.** They were drawn by painting free cells of the icon sheets (`avatar_icon_patch` in `runtime/vk_texture.c`). With every hair listed, no free cell is left (59 male and 63 female cells are used), so the default list has no story rows. `XCX_HAIR_EXTRA` can still add them with the shared placeholder icon. The gap `IconID`s (male 36, 45, 54, 63, which use `IconNo` 0) and the sheets' unused cells 59 to 63 are free, but reaching them needs a hook on the `IconNo` lookup that was not found.

**Beards and mustaches.** `MNU_AvatarCharList` has `HigeType` per face: male face 0 is 0, faces 1 and 2 are 1, faces 3 to 6 are 2, female faces 0. The category list builder (`0x76d7b0`) reads it through the getter at `0x5461e8`: category 8 (Mustache) needs 2 or more and category 9 (Beard) needs more than 0. An entry hook on that getter (`avatar_hair_hige`, only for the caller at `0x76d814`) can answer 2 for male faces. It is off by default (`XCX_HAIR_BEARDS=1` turns it on) because it does not help: the tabs then show on face 0, but the mustache chosen does not appear (also not when the model-side reader at `0x5aa43c` answers 2). On face 4 it renders. The lists have two entries each (none and one mustache or beard), `MNU_AvrIconBeardList` has two rows (the tab icons), and no resource names exist for beards (the face resource names only have head, hair and option kinds). So beards are part of each head's own assets and the younger heads may not have them, or they use a different draw. The owner notes that the single icon looks the same on every preset while the model's beard differs. Left until the fit of hair is understood.

Not done: cursor start when the current hair is a story or bonus row, female presets' lists (only checked through the tables), saving and loading with the new lists, the field's avatar editing, Windows, any fit of other-gender hair.

## Testing by script

[ui-testing.md](ui-testing.md) explains the format. What worked on a fresh profile (ticks at 60 Hz, the first frames are slow while caches fill):

```
600 A        New Game on the title screen
900 A        confirm
1400 PLUS    opening scene, skip
1500 A       confirm the skip
1701 PLUS    second skip
1800 A
2000 PLUS
2100 A       choose the first preset, the creator moves to its first tab
2400 R       next tab, a second R reaches the hair tab
```

A is also the way to open a list. The preset grid appears around frame 3300 at a limit of 120 fps. Picture size is 480x270 unless `XCX_SHOTS_FULL=1` is set, and full size is needed to read the hair tab.
