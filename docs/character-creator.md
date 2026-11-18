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
- `avatarmakedata` is written from the creator's save menu. It was 740 bytes (0x2e4) for the first male preset. Its bytes are masked: the first 0x200 bytes and the last 0xe4 bytes look random, and XOR of the two parts shows a readable record. In it the face id 58 and the hair id 72 sit next to each other as 16-bit values (offset 0x58), followed by the option id (0), as in the `DEF_PcList` columns. Floats of 1.0 and several 0x32 bytes follow. The mask itself is not worked out. A second, shorter record starts near offset 0xa0.
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
