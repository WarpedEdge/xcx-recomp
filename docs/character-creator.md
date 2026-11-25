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

## Fit of other-gender hair: bind data (loose-file test)

**What decides where a hair sits.** A hair model (`.wimdo`) keeps one 4x4 inverse-bind matrix per skin bone (columns in memory, translation in the last column, 0xb8 bytes per bone element, the matrix 0x38 bytes into the element; for the 39-bone female hair `fc212002` the first one is at +0x2d0). Each equals the inverse of the bone's bind-pose world matrix in the hair's own `_rig.skl`. The game skins the hair as pose(wearer's bone) times that inverse bind. The rig files are whole skeletons (about 150 bones) and differ by gender: the hair bones `DJ_hair*` sit about 5 cm higher in the male rig than in the female one, `JU_head` 3.5 cm lower. A hair of the other gender is therefore offset by the difference of the two bind poses, which is the bare patch of scalp. Same-gender hairs on another head differ by about 1 cm.

**Rig file header.** The bone count is at +0x58, the parent table offset (16-bit indices) at +0x50, the name table (16-byte entries, the first word a file offset) at +0x60 and the local transforms (48 bytes: position, quaternion, scale) at +0x70. That last offset is 0x120 in the hair rigs and 0x130 in the head rigs, so it must be read from the header.

**Test with loose files** (`runtime/romfs_mods.c`, a mod that replaces `chr/fc/<name>.wimdo`): every inverse bind of the female hair `fc212002` replaced by the inverse of the bind world of the male skeleton's bone of the same name. On the first male preset the bare patch is gone and the hair fits (the same screen without the mod shows it). The same change for a male hair on a female preset fits too. A loose file changes the hair for everybody who wears it, so a real fix has to act per wearer.

**What was tried at run time.** The per-frame pose feed (`main+0x3177a0`) hands each skin bone's pose matrix to the part (a 64-byte matrix on the stack at sp+0x80, then the setter at `main+0x9bc8b4`, which copies it). A hook after its last store (`main+0x317a60`) sees the part, the wearer's skeleton (`[chr+0x28]+8` is the skeleton file in memory, the head's rig) and the bone mapping by name. For every part the log shows the part's bind and the wearer's bind agree (differences under 0.001) for parts that belong to their wearer. Two things did not work: the other-gender hair never showed up in that hook, and changing the pose matrix there (lifting every bone of the male skeleton by 30 cm) did not move the character in the creator at all. So the character shown in the creator is not fed through this function, or its pose is written elsewhere afterwards. The palette builder (`main+0x9bc940`) reads the inverse binds straight from the model file in memory (`this+0x130`), and is the other place to change them.

## Pages: own gender, other gender, story characters

The hair grid shows 56 to 70 icons at once, which is too many to scroll through when it holds both genders and the story characters. The grid is therefore split into three pages, and one button turns the page while the Style category is open (`driver_fn` in `runtime/avatar_hair.c`; `XCX_HAIR_PAGING=0` turns it off, `XCX_HAIR_PAGE=0|1|2` sets the page the creator opens on). The button is Switch Y: Triangle on a PlayStation pad and the top button of an Xbox pad (the Xbox layout puts Switch Y there), the west button with the Nintendo layout, and whatever Y is bound to on the keyboard (the left mouse button by default). The right stick and the arrow keys are not used: they turn the character in the creator. Switch X is taken too (Save/Load Avatar Data), which a first attempt with X found out. One press goes to the next page and wraps from 3 to 1.

| Page | Rows | Icons |
|---|---|---|
| 0 | the face's own gender (rows 1 to 56 male, 57 to 113 female, plus the bonus rows) | the game's own |
| 1 | the other gender's rows | the other gender's icon sheet, swapped in when the sheet is uploaded (`avatar_icon_patch` in `runtime/vk_texture.c`) |
| 2 | the story characters' rows (114 to 132) | solid colours on cells the page does not use (the rows have no icon) |

Rows 115, 120 and 122 render bald or as a bare patch of scalp on every head and are left out. The face's own eight rows are hidden on pages 1 and 2 (`avatar_hair_avt`), because the game builds the list from the face's rows first.

A page change rebuilds the list (`page_apply`) and then has the game build the tab again where it stands. The game's per-frame UI update (`main+0x768d50`, hooked, `avatar_hair_tick`) runs the hair tab's own entry routine (`main+0x76d45c`: the tab word, the category list with the hair list, the grid) on the game's thread while a request is waiting, with the UI object's state word set to 11 (it is 0 afterwards). The tab is not left, so the camera and the other panels stay put. The first version hopped to the Eyes tab and back (`page_bounce`: R then L, injected through `input_inject` in `runtime/input.c`), which showed the other tab for a moment and moved the camera; `XCX_HAIR_BOUNCE=1` brings it back. The entry routine leaves the focus on the category list, so the helper thread presses A again when the grid had the focus. `XCX_HAIR_STATE` sets the state word the routine is given (`0xffffffff` leaves it).

**The hint.** A small panel at the bottom left of the picture reads "Change page" and the page's name and number (for example "Female hairstyles 2 / 3"), with the glyph of the page button in front. It is drawn by the host overlay (`hint_draw` in `runtime/ui/menu.cpp`). The glyph comes from the glyph system: `glyph_current_symbol_set` gives the art tile for Switch Y in the style in use, so a PlayStation pad shows the Triangle tile, an Xbox pad the Y tile, the keyboard style the mouse button tile, and the native style a plain round "Y". The overlay has no textures, so the tile is resampled to the size it is drawn at, in whole pixels (`glyph_art_cell`), and drawn as runs of one-pixel rectangles at whole positions (a fixed 64 x 64 fit stretched by a fraction looked grainy). The panel stays while the F1 menu is open. The panel shows only while the Style category is open (the category cursor, UI word `+0x708`, is 0), so it does not claim a button the colour grids do not use. Checked in the harness with `XCX_GLYPH_STYLE=playstation` (Triangle) and `keyboard` (mouse button); it is not an inline glyph of the game's own help line, which is built by the game and has no room for an extra entry.

**Marks on the icons.** A hairstyle that still does not fit has a small red square with a white X at the bottom right of its icon (since the fix below only the stubble and buzz rows of male hair on a female head). The marks are painted into the icon sheet like the story characters' colours (`badge_paint`, `bc7_two` in `runtime/avatar_hair.c`: BC7 mode 6 blocks of two colours), and a row's cell is `icon_no[icon0 + row - r0]`. The lists are `bad_female_on_male` (empty), `bad_male_on_female` and `bad_buzz`. A hairstyle that looks wrong without a mark is a fit that did not apply.

**Preset numbers on the icons.** Every hairstyle of the lists of the two genders carries two small white numbers at the bottom left of its icon: the numbers of the two presets of the creator's first screen (1 to 14 in the order of its grid, per gender) that share the face the hairstyle was made for, for example "1 2" for the hairs of the first face. They are painted into the icon sheet like the marks (`number_paint` in `runtime/avatar_hair.c`: the blocks that hold the digits are two-colour BC7 blocks of the sheet's dark background, so only the digits show; `face_preset` gives the first preset number of each face). The story characters' page has none, because its colours use free icon cells.

## Fit of other-gender hair: what the review shows

**The fix in use.** `avatar_hair_ibind` (after-hook at `main+0x9bca1c`, the palette builder's read of an inverse bind) replaces each hair bone's inverse bind by the inverse of the wearer's rig bind of the bone of the same name, and by the head joint's offset for a bone the wearer's rig lacks. Same-gender hairs change by about 1 cm and fit as before. A hair of the other gender then sits where it was cut. On top of that, a hair cut for the other gender (`other_gender_hair`: a `DJ_hair*` bone or `JU_head` of the part sits 2.5 to 20 cm higher or lower than in the wearer's rig) is scaled and lifted about the wearer's head joint: female hair on a male head by a factor of 1.1 and 1 cm up (`tune_m` in `runtime/avatar_hair.c`), male hair on a female head not at all (`tune_f`, 1.0). `XCX_HAIR_SCALE`, `XCX_HAIR_LIFT` and `XCX_HAIR_FORWARD` override both.

**Two mistakes that hid the fit** (both fixed). The wearer's skeleton is taken from the pose feed, and the slot for the female skeleton was filled by any skeleton of more than 100 bones whose head joint is above 1.625 m. Another skeleton, whose head joint is at 6.5 m, took the slot, so no male hair was fitted on a female head at all and it sat about 4 cm too high with bare temples. Now only a head joint at 1.595 to 1.62 m (male, 1.608 m) or 1.635 to 1.655 m (female, 1.643 m) counts. Second, the decision to scale a hair used the hair's own `JU_head` bone, which several hairs lack, so those (102, 105, 111 among them) were never scaled.

**The real cause of the worst cases** (found on 2026-11-21): `is_hair_model` called a model a head, and so did not fit it, when it listed any `FC_` bone. A hair with long bangs lists a few of them (`FC_bwB_L`, `FC_bwB_R`, `FC_bwC_L`, `FC_bwC_R`, `FC_nose`, `FC_jaw`) because the bangs follow the brows, so those hairs were never fitted at all: the twin curls (59, 66), 74, 84, 91, 99, 102, 103, 106, 108, 111, 112 and 133, the "cap inside the skull" of the earlier lists. A real head model lists `FC_face`; that is now the test (`FC_face` in `is_hair_model`). Every female hair then fits a male head. Two more facts found on the way: the binds of a hovered preview are already the wearer's (the game retargets them), so `other_gender_hair` never sees a hovered hair as the other gender's, and page 1 is therefore treated as other-gender by definition (`page == 1` in `avatar_hair_ibind`); and the debug log (`XCX_HAIR_FIT=debug`) now lists each model's first table entries and the FC_ bones of a model that is not taken for a hair.

**What is left** (every item of every page reviewed with the current build; the pictures are in the local harness output):

- Female hair on a male head: none.
- Male hair on a female head: rows 26, 33, 44, 45, 46, 49, 50, 53, 54 and 56 show skin where a male head shows stubble (short crops and shaved heads: the hair's own scalp shading is missing on the female head, a bind cannot supply it), and rows 34, 47 and 55 are buzz cuts that show bare scalp with the game's own sparkle on every head. They carry the X. Rows 8 and 14 looked bare before the fix and fit now.
- Own gender, female: rows 98 to 113 (the hairs of the faces 12 and 13) left a bare patch on the crown when worn by the other female faces (checked on faces 7 and 10), with the fit on or off, so it is the game's own cut. `cap_fix_wanted` gives them a scale of 1.1 and a lift of 1 cm about the head joint while their row is under the grid's cursor; the patch is gone. The faces 12 and 13 themselves are left alone.
- Own gender, male: fits on the first face; story characters (page 2) fit on both heads (the sparkle on 132 is the game's own).

**Labels.** The earlier lists (58, 68, 69, 83 ... for the female hair, 7, 25, 43 ... for the male hair) were one row too low, and some items were missed because the review's cursor skipped them. `review_run` now moves to item k one press at a time and waits for each press to show, so every item is photographed and the label is the row of the grid entry at that position (`grid_order`). A pick through the page button on a male head confirmed it: the curls at item 9 were row 66, the grid entries are in ascending row order (57, 58, 59 ...), and the avatar wears the hair of the entry under the cursor. `XCX_HAIR_REVIEW_WIDE=1` saves the whole frame instead of the head and `XCX_HAIR_REVIEW_MAX=n` stops after n items of a page.

**Page test.** `XCX_HAIR_PAGETEST=<name>` (with `XCX_HAIR_PAGETEST_N` presses, default 4, and `XCX_HAIR_PAGETEST_PICK=1` to pick a hairstyle on each page) presses the page button itself once the grid has the focus, saves a shot of the whole frame after each and logs the page, the cursor, the entry row and the UI words. `XCX_HAIR_HINT_SHOT=n` saves up to n frames that include the overlay.

Not solved: the check mark of the grid stays on the position of the current hairstyle even when that hairstyle is not on the page that is shown; the grid's focus is on the category list after a page change unless the helper thread's A press lands.

## The review run

Instead of looking at every hairstyle by hand, the game can visit them itself (`XCX_HAIR_REVIEW=<name>`, `review_run` in `runtime/avatar_hair.c`). The creator is reached by an input script (`tools` in the local harness, see below), then for each hairstyle of each page the game waits for the model to settle, saves a crop of the head (`boot_shot_request`, `runtime/boot.c`), moves the cursor to the next item by an injected press, then ends the process after the last. `XCX_HAIR_REVIEW_PAGES=3` visits all three pages, `XCX_HAIR_TUNE="scale,lift,forward;..."` photographs each hairstyle once per tuning set. The pictures are named `<run>_p<page>_i<item>_r<row>.png` (with `_t<set>` for tuning runs), so a failing picture tells row and item at once.

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

Facial-hair implementation research: [character-creator-beards.md](character-creator-beards.md).

## Modify: fitting a hair by hand

The hair tab's category list (Style, Special, Roots & Tips Color, Roots Color, Tips Color, and Mustache and Beard on the faces that have them) is a list widget over a vector of 36-byte items at `widget + 0x498` (the widget is the creator UI's word at `+0x2918`): a label message id at `+0`, the kind at `+0xa` (6 Style, 7 Special, 8 Mustache, 9 Beard, 0xa to 0xc the colours). The tab's builder (`main+0x76d7b0`) fills the vector and then calls the widget's layout (`main+0x5ad99c`), which picks a panel with as many rows as items. An after-hook on `main+0x76dd54` (`avatar_hair_cats`) appends one item of kind 0xd, a copy of the Style item (see "The Modify row's picture" below; at first it was a copy of the Roots & Tips Color item, which showed a colour swatch), so the list gets a Modify row as the Mustache and Beard rows do. The game draws it with its own font, highlight, cursor and sounds; the grid beside it is empty for a kind it does not know.

- The label: the item carries the message id of the game's "Load Game"; an entry hook on the text setter `main+0x917160` (`avatar_hair_text`, return address `main+0x5ad6a0`, the list widget) sets "Modify" through the raw-text setter `main+0x91720c` instead.
- The window is drawn by the host overlay (`modify_draw` in `runtime/ui/menu.cpp`) over the grid's place while the cursor is on the row, in the look of the game's own panels (an even dark body, a slate header band with a raised notch and two small squares, a thin bottom band, the bright cyan frame on the selected row). It lists every part of the hair under the cursor (whole hair, scalp cap, front, sides, tails, other strands; only the parts the hair has bones for once they are known) with Scale, Left / right, Up / down, Back / forward and Tilt each, every value on a slider like the creator's own (thin track, lit up to the thumb, a glowing thumb on the selected row). The sliders end at x0.5 and x1.5, 15 cm either way and 180 degrees either way (the whole turn). A on the row (the game itself takes the focus into its empty grid, or the helper thread sees the press) opens it: the game's input is blocked except the right stick. Up and Down walk the rows, Left and Right change a value (hold to repeat; ZL fine, ZR coarse), L and R jump to the previous or next part, Y sets a row back, X sets all back, B closes. Closing hands the focus back to the category list through the UI update hook (`main+0x76d45c` run with state 13; state 12 would focus the grid).
- Input: the window reads the pad's d-pad and its left stick alike. On the keyboard the d-pad is the keys 1 to 4, the left stick WASD and the right stick (the camera) the arrow keys, so WASD steers the window and the arrows still turn the character.
- Faces 3 to 6 already have 7 rows, and the list widget lays out and walks at most 7 (an 8th crashed the game in `main+0x5ad610`: it has 7 row panes `mc_List01` to `07` and 7 row slots). On those faces the vector holds a window of 7 of the 8 items (`virt_off` 0 or 1): the cursor wrapping from the last row to the first (or back) is seen in the UI update hook, which builds the tab again in place with the other window and the cursor on the new end. Style scrolls off the top when the cursor goes down to Modify. While the list is windowed a scroll bar is drawn at the right edge of the list (`listbar_draw`, the colours of the grid's bar, a thumb of 7/8 of the track).
- The game's "Discard current edits and return to the base selection menu?" question (B on the category list) is a panel of the game's own picture. The creator's state word `ui+0x68` reads 41 while it is up (`dialog_up`); the painting around it is still marked up by a B press on the category list until the next A or B (`dialog_hide`). While it is marked, the window is painted in the pieces around the question's rectangle (0.197 of the grid's width in, 0.262 of its height down, 1.56 widths and 0.661 heights across) so the question sits over it as it sits over the colour grid, and the page hint is hidden.
- Sounds: the creator's UI object keeps sound ids at `+0x610` to `+0x630`; `main+0x7788dc(ui, flag)` plays the one at `+0x610` (flag 0) or `+0x628` (flag 1) through the sound queue `main+0x40bc48(manager at [0x7001d57598], id, 0)`. Which of them are the confirm and back sounds of the lists is not known; the window plays them as a guess, and value steps play the slot `+0x61c` (the slot the seven slider-like widgets at `main+0x779200` to `+0x7796cc` use). A hook on `main+0x40bc48` (`avatar_hair_snd`) logs every sound the hair tab plays, with its return address, as `avatar hair: sound N played from ...`, and the log names the ids in the slots once, so the real ones can be read off a run.
- Where a change acts: the "All" and "Main" (scalp cap) parts' changes are a scale, tilt and shift about the middle of the skull (0.10 m above the head joint; the fit's own tuning for a hair cut for the other gender keeps its pivot at the head joint), a strand part's (front, sides, tails, others) about its own root, the highest bind position among its bones. A pivot at the head joint made the hair float up and bare the forehead. The changes act on every hairstyle, not only those cut for the other gender. On a hair like the default male one almost everything is skinned to the head joint, so "Main" (the scalp cap) scales the whole hair and the other parts are small (one front bone).
- The changes belong to the hairstyle they were made on (`mod_store`, keyed by the row and the wearer's gender); back at the base or gender selection (the UI's tab word is 1 or 2, the tabs are 3 and up) the fit and all changes are cleared, otherwise the next gender's model was fitted with the old face's skeleton and the old values.
- The hair pages: the page is reset to the wearer's own gender's when the base face changes and when the hair tab is left. The grid's check mark and starting cursor follow the game's own position word (`ui+0x70c`, the position of the worn hairstyle in the list built for the current page): on a page that does not list the worn hairstyle the mark lands on a stale position. Clearing it was tried through `grid+0x870`, `grid+8` and `ui+0x70c` and did not remove the mark (the cells keep their own state), so it is not solved.
- Test helpers (XCX_HAIR_MODVIS=<name>, run with `build/inst/tools/modvis.sh`): sets each Modify value by itself and saves shots of the hair from the front and the side; XCX_HAIR_PAGEVIS=1 does the same for the pages and a confirmed hairstyle; XCX_HAIR_MODVIS_SPECIAL=1 adds the Special rows.
- Every change is logged as `avatar hair modify: ...` with the absolute values of all parts. `XCX_HAIR_MODIFY=0` turns the row off.
- Checked in the harness (male face 0, female face 7, face 0 with `XCX_HAIR_BEARDS=1` for the full list) with `build/inst/tools/modnav.sh`: the walk from the style grid to the row, A, moving, changing values, B. Played by hand on the keyboard. Not checked with a real pad.

### Recording the hand-made fits

Every time a Modify window closes, or the cursor leaves a hairstyle whose values changed, one line is appended to `<profile>/hair-modify-record.txt`: the hairstyle's row, the wearer's gender, the page, and the total of every part that is not at its start (scale as a factor, shifts in cm, tilt in degrees), or `default`. A later line of a hairstyle replaces an earlier one. The fits found this way are meant to become the starting point of each hairstyle: `runtime/avatar_hair_fits.inc` holds a table (`mod_baked`, key = row * 2 + 1 for a female wearer, else row * 2; scale change, metres, degrees per part). The window loads the entry of the hairstyle under the cursor into `mod_b` and shows it as scale 1.0 and no shift or tilt; the player's changes are added to it (`mvc` in `runtime/avatar_hair.c`), and Y or X sets a row back to that start. The table holds the hand fits of the male hairstyles (next section). Limits: the start values apply where the hairstyle is the one under the cursor in the creator; a worn hairstyle seen anywhere else has only the fit of its gender (the model carries no row to look the table up by).

### Fit by candidates (female hair on a male head)

After the Modify window and its start values existed, every hair of the lists was photographed on the first male preset and the first female preset (three views each: `XCX_HAIR_REVIEW=<name>` with `XCX_HAIR_REVIEW_PAGES=3 XCX_HAIR_ANGLES=2 XCX_HAIR_UNTURN=1`, run through `build/inst/tools/shotr.sh`; the right stick turns the model one step between shots and `XCX_HAIR_UNTURN=1` turns it back, though the view drifts a little over a long run). What the pictures show:

- Hairs of the wearer's own gender (male rows 1 to 56 and the bonus rows on a male head, female rows on a female head) fit. The buzz cuts (rows 34, 47, 55) have the game's own sparkle, and the shaved styles have bare scalp by design.
- Male hair on a female head fits, except for the rows that only lack stubble shading (known, see "Fit of other-gender hair").
- Female hair on a male head had a hairline that sat too far back: the scalp cap is cut for a smaller, lower forehead, so the forehead and crown showed bare skin (worst on the ponytails and updos: rows 98, 101, 109, 112, 86, 87, 94, 96, 103, 105; milder on the curls 59 and 66 and the parted styles 78, 71, 73, 95, 99, 104, 111). The story hair of row 132 had the same fault.

To pick the values, `XCX_HAIR_MODSETS="g,scale,x,y,z,tilt+g,...;..."` makes the review photograph each hair once per candidate (g is the part, 0 whole hair, 1 scalp cap; scale as a change, shifts in metres, tilt in degrees; the candidates are the start values of that hair), `XCX_HAIR_ROWS="98,101,..."` limits the run to some rows, and `XCX_HAIR_TURN=n` with `XCX_HAIR_UNTURN=1` takes the shots from n steps round and turns back. Candidates tried on the scalp cap: forward 1.5 cm (covers most of the bare forehead, a moving the cap back makes it worse), up 1.5 cm (no use), scale +0.12 (covers it), scale +0.12 with 1.5 cm forward (best), scale +0.18 or +0.25 (the cap bulges), the whole hair 2 cm forward (covers the forehead but pushes the ponytail with it). Two sets were kept, in `runtime/avatar_hair_fits.inc`:

| Start values of the scalp cap | Rows (male wearer) |
|---|---|
| scale +0.12, 1.5 cm forward | 59, 66, 78, 86, 87, 94, 96, 98, 101, 103, 105, 109, 112, and the story hair 132 |
| scale +0.08, 1 cm forward | 71, 73, 95, 99, 104, 111 |

The other female hairs looked right as they were and have no entry; the stronger set puffed them up (a pompadour on 99, 100, 106 to 108, 110). Checked on the first male preset in front and side views, and on faces 1, 3 and 6 for the worst rows. **Dropped again (2026-11-24):** a test in the game showed these start values over-scaling the hairs and leaving them off the head, so `runtime/avatar_hair_fits.inc` is empty again and the table is to be filled from hand fits, one hair at a time (the list above is only what the candidates showed). The values are start values: the Modify window shows them as scale 1.0 and no shift, and a hair of the creator that is worn on the field has only the gender-level fit, because the field's model carries no row to look the table up by.
### Hand fits of the male hairstyles (2026-11-24)

Every hairstyle of the male list was put on the first male preset (face 0) and fitted by hand with the Modify window, one by one; the last line of each in the record (`hair-modify-record.txt`) is the fit. The result is `runtime/avatar_hair_fits.inc`: the values are the start values of the hairstyle, so the window shows scale 1.0 and no shift for it and the player's changes come on top. They apply on the male faces 0 to 2 (presets 1 to 6) to every hairstyle except the eight that face was made with (rows 1 to 8 on face 0, 9 to 16 on face 1, 17 to 24 on face 2: those sit right on their own head and were not fitted). The male faces 3 to 6 and the female wearer have no start values yet. Limit: they apply to the hairstyle under the cursor in the creator; a hairstyle worn on the field has only the gender-level fit.

Each table entry carries a `faces` bit mask (bit n = male face n) next to its key, and the first entry whose key and face match is used, so a refinement for some faces is put in front of the entries it replaces. The fits above are the entries with mask 0x7 (faces 0 to 2). In front of them stand the refinements made on face 1 (presets 3 and 4, whose head is a little larger than face 0's) for 24 hairstyles, and a +4% scale for rows 1 and 2, all with mask 0x6 (faces 1 and 2); face 2 (presets 5 and 6) uses them until its own adjustments exist. A line of the record carries the face (`row N (grid row R item C) wearer male face F page P: | All scale ...`) so the totals of one face can be told from another's; lines from before that field have no face.

Positions are the grid of page 1 (four icons a line, counted from 1; the row number is the hairstyle's id in `ITM_AvatarHairList`). Shifts are the change the hairstyle needed, in cm, tilt in degrees; "All" is the whole hair, the other parts only where named. What stood out: nearly every hairstyle made for another face needed the whole hair about 0.2 to 2 cm lower, the ones of the later faces also a little larger (up to x1.16) and forward, and most of the "5 6" and "7 8" ones a few degrees of forward tilt. Left alone: rows 1 to 8 (made for face 0), row 15 (no change needed), and the buzz cuts 34, 47 and 56, which show a sparkle over the scalp (see "Marks on the icons"; cause not found yet) and were not fitted. Row 55 has the same sparkle on one side only and was fitted.

| Grid position | Row | Changes from the start |
|---|---|---|
| row 3, item 1 | 9 | All: up/down -1 cm |
| row 3, item 2 | 10 | All: up/down -0.2 cm |
| row 3, item 3 | 11 | All: up/down -0.8 cm, back/forward +0.2 cm |
| row 3, item 4 | 12 | All: up/down -0.6 cm, back/forward -0.2 cm |
| row 4, item 1 | 13 | All: scale x1.01, up/down -0.4 cm, back/forward -0.4 cm |
| row 4, item 2 | 14 | All: scale x1.03, up/down -0.8 cm |
| row 4, item 4 | 16 | All: up/down -0.4 cm, back/forward -0.2 cm |
| row 5, item 1 | 17 | All: back/forward -0.2 cm, tilt +2 deg |
| row 5, item 2 | 18 | All: up/down -1.2 cm, tilt +2 deg |
| row 5, item 3 | 19 | All: up/down -1 cm, tilt +3 deg |
| row 5, item 4 | 20 | All: up/down -0.6 cm, back/forward -0.2 cm |
| row 6, item 1 | 21 | All: up/down -1.2 cm, back/forward -0.2 cm |
| row 6, item 2 | 22 | All: up/down -1.4 cm, tilt +3 deg |
| row 6, item 3 | 23 | All: up/down -0.6 cm, back/forward -0.2 cm, tilt +2 deg |
| row 6, item 4 | 24 | All: up/down -1.4 cm, tilt +2 deg |
| row 7, item 1 | 25 | All: scale x1.01, up/down -1 cm, back/forward +0.4 cm, tilt +1 deg |
| row 7, item 2 | 26 | All: up/down -1.8 cm, back/forward +1 cm, tilt +4 deg |
| row 7, item 3 | 27 | All: scale x1.02, up/down -1.6 cm, back/forward +0.6 cm, tilt +3 deg |
| row 7, item 4 | 28 | All: scale x1.02, up/down -1.6 cm, back/forward +0.6 cm |
| row 8, item 1 | 29 | All: up/down -1.6 cm, back/forward +0.6 cm |
| row 8, item 2 | 30 | All: up/down -1.4 cm |
| row 8, item 3 | 31 | All: up/down -1.6 cm, back/forward +0.6 cm |
| row 8, item 4 | 32 | All: scale x1.02, up/down -1.6 cm, back/forward +0.2 cm |
| row 9, item 1 | 33 | All: scale x1.08, up/down -1 cm |
| row 9, item 3 | 35 | All: scale x1.02, up/down -1.6 cm, back/forward +0.6 cm, tilt +3 deg |
| row 9, item 4 | 36 | All: scale x1.01, up/down -1.8 cm, back/forward +0.6 cm |
| row 10, item 1 | 37 | All: scale x1.03, up/down -1.6 cm, back/forward +0.2 cm, tilt -2 deg |
| row 10, item 2 | 38 | All: up/down -1 cm |
| row 10, item 3 | 39 | All: scale x1.01, up/down -0.6 cm, back/forward +0.4 cm, tilt +1 deg |
| row 10, item 4 | 40 | All: up/down -1.4 cm, back/forward +0.4 cm |
| row 11, item 1 | 41 | All: scale x1.16, up/down -1.4 cm, back/forward +0.4 cm, tilt -1 deg; Sides: back/forward -0.2 cm; Other strands: scale x0.75 |
| row 11, item 2 | 42 | All: scale x1.06, up/down -1 cm, back/forward +1.2 cm |
| row 11, item 3 | 43 | All: scale x1.07, up/down -0.6 cm, back/forward +0.4 cm, tilt +2 deg |
| row 11, item 4 | 44 | All: scale x1.04, up/down -1.2 cm, back/forward +0.4 cm, tilt -2 deg |
| row 12, item 1 | 45 | All: scale x1.09, up/down -1.4 cm, back/forward +1 cm, tilt -1 deg |
| row 12, item 2 | 46 | All: scale x1.05, up/down -1.2 cm, back/forward +0.6 cm |
| row 12, item 4 | 48 | All: scale x1.02, up/down -1.6 cm, back/forward +0.6 cm, tilt +3 deg |
| row 13, item 1 | 49 | All: scale x1.14, up/down +0.2 cm, back/forward +1.2 cm, tilt +3 deg |
| row 13, item 2 | 50 | All: scale x1.13, back/forward +0.4 cm |
| row 13, item 3 | 51 | All: scale x1.1, left/right +0.2 cm, up/down -0.2 cm, back/forward +0.8 cm, tilt +5 deg |
| row 13, item 4 | 52 | All: scale x1.03, back/forward +0.8 cm, tilt +2 deg |
| row 14, item 1 | 53 | All: scale x1.13, back/forward +0.4 cm, tilt +1 deg |
| row 14, item 2 | 54 | All: scale x1.14, up/down +0.2 cm, back/forward -0.2 cm, tilt +3 deg |
| row 14, item 3 | 55 | All: scale x1.12, up/down +0.8 cm, back/forward +0.4 cm, tilt +4 deg |
| row 15, item 1 | 134 | All: up/down -0.8 cm |
| row 15, item 2 | 135 | All: scale x1.03, up/down -0.6 cm, back/forward -0.2 cm |
| row 15, item 3 | 136 | All: scale x0.98, up/down -2.2 cm, tilt -2 deg |


### The Modify row's picture (2026-11-25)

The Modify item is a copy of the item of another row, and the game draws that row's picture for it: a copy of the Style item shows the worn hairstyle's thumbnail, and it follows when another hairstyle is confirmed (checked by hand on male faces; the colour rows' items instead give a plain swatch). Of the 36 bytes of an item, the label id (`+0`) and the kind (`+0xa`, 0xd for Modify) are replaced, and the count (`+4`) is kept unless the template is a swatch row (the flag byte at `+0x20` is 1 for the colour rows, 0 for Style and Special). The kind stays 0xd so the game still treats the grid as empty and the host overlay draws the window. `modify_icon_kind` in `runtime/avatar_hair.c` names the kind of the row to copy: 6 (Style) now; `XCX_HAIR_MODICON=0` brings back the swatch. It is read when the tab's list is built, an event and not a per-frame or per-draw step (see the hot-path rule in `AGENTS.md`).

The Mustache (kind 8) and Beard (kind 9) rows have items of their own, so the same copy would give the Modify row a mustache or beard picture. That is not wired up: the row always follows Style, because the window edits hairs only. When a fitting for facial hair exists, the intended rule is that the row shows the category the player last entered (confirmed), and the window edits what that row shows. The category's kind is at `item + 0xa` of the item under `ui + 0x708`; entering it is the focus moving into the grid (`grid_focus`). Changing the picture while the tab stays open means rewriting the Modify item in the vector in place, then the game's own row refresh; this has not been tried.

### Fixes of 2026-11-25

- Answering the "Discard current edits" question with A (Cancel, or Return to Base Selection) also opened the Modify window on the row under it, because the helper thread saw an A press on the Modify row. The row now ignores an A or Minus press while the question is up and for 300 ms after it closes (`dlg_recent` in `modify_input`), which also covers the game closing the question before the thread sees the press.
- The category list's scroll bar is thinner and sits wholly inside the list panel (`listbar_draw` in `runtime/ui/menu.cpp`).
- The Modify window and the scroll bar are placed with fixed fractions of the window's width, measured with the owner's `xcx.ini` (window 2560x1440, game resolution 3440x1440, `picture_fit = stretch`). A test instance without that file (default 1920x1080 and `picture_fit = keep`) showed them shifted to the right; copy the file into any other instance the overlay is judged in. Placing them from the game's own grid rectangle, so they hold at any setting, is not done.
- Same-gender hair keeps its own bind, and a hair the cursor leaves keeps its values while it fades out (`sw_gen`); the check mark follows the game's list position (`fresh_pick`). These were fixed on 2026-11-24 and are described with the fit above.
