# Character creator: hairstyles

How the game's character creator builds its hairstyle list, what stores the choice, and what that means for offering more hairstyles. It is the base for making every hairstyle in the game choosable ([game-data-tables.md](game-data-tables.md) explains how the tables were read).

Status: the tables and the code were read for the 2.0.0 `main`. The first male preset's hair tab was checked in the game on Linux with scripted input. No hook exists yet. Nothing was run on Windows. Items marked "not checked" were not tested.

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

Next: find where the game places a hair part on the head, then add a test-only way to change its size and position while the game runs.

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
