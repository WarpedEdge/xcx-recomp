# Save slot pages

The game's Save and Load screens show the auto save and three manual slots. Pages give the player 30 manual slots (10 pages of 3) on those same screens, without changing their layout.

## What the player sees

- L and R change the page while the screen is idle (rows loaded, no message open). The page wraps around.
- The screen title ends with the page between the game's own L and R button icons, for example `<title>   [L] Page 2/10 [R]`. The icons are drawn by the game's inline icon text, so they follow the Button Icon Type setting (Nintendo, PlayStation, Xbox, Steam, keyboard), also with no controller connected.
- The open part of the title bar, past the small slash, shows the page's name: `Page 2` until the player gives it another. Y (a controller's Y, or the left mouse button, which is Y by default) opens the host's text entry, the one the game's character naming uses; its keyboard works with a controller. Up to 24 characters; empty text puts `Page N` back.
- Page names are kept in `save-pages.txt` in the profile directory (beside `save/`), one `page<TAB>name` line each. They are not in the save folder: the game's save mount is journaled, and a file the game did not write there is dropped at the next commit.
- The same file keeps a `last<TAB>page` line (1-based) with the page the screen was left on. It is written on every page change, read the first time the screen opens (before the rows load) and absent while the page is 1, so a launch opens on the page the player was last on.
- A page change plays the sound the game's own screens play when L or R switches a tab.
- Each manual row shows its slot number in the corner where the auto save row has its label (`No. 04`). An empty row shows the number after its usual "no save" text.
- The auto save is one slot for every page. Continue on the title screen always loads it.
- A new profile starts on page 1, which is the game's own slots 01 to 03.

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
| `0x55f548` (entry) | `rt_save_pages_label_entry` | the label setter the frame's title goes through: keeps the frame layout's handle |

The title and the labels are set with the game's own text functions (`rt_call`), so they use the game's font. They are English in every language. The sound is the one the game's own L/R tab screens play. The functions involved are listed in [ui-widgets.md](ui-widgets.md).

## The title bar and the page name

The page name is drawn in `rect_TextTitle03`, a text pane of the screen frame that these screens leave empty, and `rect_TextTitle02` holds the blank spaces that centre it in the box right of the slash. `title_label` and `name_spacer` in `runtime/save_pages.c` set them. How the panes were found, what they look like and what was tried to make the name match the title's font (it does not: the pane uses the smaller, grayer sub-title style) are in [ui-widgets.md](ui-widgets.md). The title text itself is unchanged apart from the page label.

## Limits

- 30 manual slots (`SAVE_PAGE_COUNT` in `runtime/save_pages.h`). The file names allow more; the screen shows three at a time, so a higher count only adds pages.
- Each slot is about 0.9 MiB (the save file and its thumbnail), so 30 slots add about 27 MiB. A save commit links unchanged files; a commit with 19 files took 51 ms on Linux.
- The screen's own arrays hold four rows, so the page window is what makes more slots possible without editing the game's layout.

## Checks

- `tests/save_pages_check.py` runs the page range, the name mapping, the label and the page names (cleaning, limit, file format; no game files needed).
- `python3 recomp/gen.py` stops with a message when an instruction word at a site differs.
- By hand with a profile that has saves: open Load Game on the title screen, change pages with L and R, rename a page with Y, save from the in-game menu on a later page, and load a slot from it.
- Without touching the controller: [ui-testing.md](ui-testing.md) shows how the Linux checks were done with scripted presses and screenshots (page change, rename, controller icon styles).

The buttons the screen reports (Y, L and R are used here), the sound and the text entry are described in [ui-widgets.md](ui-widgets.md). A Y press is reported again on the frame the text entry closes, so `rt_save_pages_pad` ignores Y for 30 hook calls after one.
