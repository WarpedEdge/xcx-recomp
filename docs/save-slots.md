# Save slot pages

The game's Save and Load screens show the auto save and three manual slots. Pages give the player 30 manual slots (10 pages of 3) on those same screens, without changing their layout.

## What the player sees

- L and R change the page while the screen is idle (rows loaded, no message open). The page wraps around.
- The screen title ends with the page between the game's own L and R button icons, for example `<title>   [L] Page 2/10 [R]`. The icons are drawn by the game's inline icon text, so they follow the controller style the game uses elsewhere.
- A page change plays the sound the game's own screens play when L or R switches a tab.
- Each manual row shows its slot number in the corner where the auto save row has its label (`No. 04`). An empty row shows the number after its usual "no save" text.
- The auto save is one slot for every page. Continue on the title screen always loads it.
- The page is kept until the game closes. A new run starts on page 1, which is the game's own slots 01 to 03.

## How it works

The game keeps asking for `gamedata01` to `gamedata03` and their `.tmb` thumbnails. `runtime/save_pages.c` holds the current page, and `host_path_kind` in `runtime/fs_host.c` rewrites those three names for pages after the first: page 2 is `gamedata04` to `gamedata06`, page 10 is `gamedata28` to `gamedata30`. Loading, saving and creating a slot all go through that function, so the game needs no knowledge of pages. Page 1 uses the game's own file names, so existing saves keep working and a build without the hooks reads the same files.

The screen reads its rows when it opens. On a page change the screen's loading state is started again (`save_pages_reload`): the rows are marked requested, the loading flag is set, and the screen's own code reloads them from the files of the new page and redraws.

The hooks are emitted by the generator for the 2.0.0 `main` only, and only when its build ID, hash and the instruction words at every site match (`recomp/save_pages.py`). Another `main` is lifted without them.

| Site (main offset) | Hook | Use |
| --- | --- | --- |
| `0x6661c0` | `rt_save_pages_pad` | the buttons pressed this frame, L and R change the page |
| `0x6665c0` (entry) | `rt_save_pages_rows` | slot numbers on the rows, page in the title |
| `0x6660a0` (entry) | `rt_save_pages_update` | plays the page change sound the frame after the pad hook asked for it |
| `0x6654a0` (entry) | `rt_save_pages_open` | a screen was built: look for its title next |
| `0x916d44` (entry) | `rt_save_pages_text` | takes the title widget and the empty-row text when the game sets them |

The title and the labels are set with the game's own text functions (`rt_call`), so they use the game's font. They are English in every language. The sound is the effect the game requests as `(manager, 5, 0)` through the sound request at `main+0x40bc48`, the one its own L/R tab screens use.

## Limits

- 30 manual slots (`SAVE_PAGE_COUNT` in `runtime/save_pages.h`). The file names allow more; the screen shows three at a time, so a higher count only adds pages.
- Each slot is about 0.9 MiB (the save file and its thumbnail), so 30 slots add about 27 MiB. A save commit links unchanged files; a commit with 19 files took 51 ms on Linux.
- The screen's own arrays hold four rows, so the page window is what makes more slots possible without editing the game's layout.

## Checks

- `tests/save_pages_check.py` runs the page range, the name mapping and the label (no game files needed).
- `python3 recomp/gen.py` stops with a message when an instruction word at a site differs.
- By hand with a profile that has saves: open Load Game on the title screen, change pages with L and R, save from the in-game menu on a later page, and load a slot from it.
