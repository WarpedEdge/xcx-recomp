# Save slot pages

The game's Save and Load screens show the auto save and three manual slots. Pages give the player 30 manual slots (10 pages of 3) on those same screens, without changing their layout.

## What the player sees

- L and R change the page while the screen is idle (rows loaded, no message open). The page wraps around.
- The screen title ends with the page between the game's own L and R button icons, for example `<title>   [L] Page 2/10 [R]`. The icons are drawn by the game's inline icon text, so they follow the Button Icon Type setting (Nintendo, PlayStation, Xbox, Steam, keyboard), also with no controller connected.
- The open part of the title bar, past the small slash, shows the page's name: `Page 2` until the player gives it another. Y (a controller's Y, or the left mouse button, which is Y by default) opens the host's text entry, the one the game's character naming uses; its keyboard works with a controller. Up to 24 characters; empty text puts `Page N` back.
- Page names are kept in `save-pages.txt` in the profile directory (beside `save/`), one `page<TAB>name` line each. They are not in the save folder: the game's save mount is journaled, and a file the game did not write there is dropped at the next commit.
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
| `0x55f548` (entry) | `rt_save_pages_label_entry` | the label setter the frame's title goes through: keeps the frame layout's handle |

The title and the labels are set with the game's own text functions (`rt_call`), so they use the game's font. They are English in every language. The sound is the effect the game requests as `(manager, 5, 0)` through the sound request at `main+0x40bc48`, the one its own L/R tab screens use.

## The title bar and the page name

The title is a text widget of the frame layout, and it squeezes its whole text to its own width, which ends at the small slash. A page name appended to the title therefore compresses the title once it is longer than about eight characters, and the box to the right of the slash is not part of that widget. The frame layout does hold two more text panes that the Save and Load screens leave empty, `rect_TextTitle02` and `rect_TextTitle03`, found by looking up about 1000 pane names from the rodata in the frame's layout (`rect_TextTitle`, the title itself, answers too). They chain: `rect_TextTitle03` starts where `rect_TextTitle02`'s text ends, and `02` sits lower than the title line. So `02` gets blank spaces and `03` gets the name, which lands in the empty part of the bar at the title's height. Each space moves the name about 9 pixels (at 1920 wide, measured from screenshots with 20 and 40 spaces) and with none it starts at pixel 355. The number of spaces is chosen so the name is centred in the bar: `name_spacer` in `runtime/save_pages.c` estimates the name's width by kind of character (the font is proportional) and sets the blanks to put its middle at the middle of the empty part. The estimate is good to a few characters' width, not exact. The title text itself is unchanged apart from the page label.

What did not work, so nobody repeats it:

- Writing floats into the title widget's fields to widen it: the fields near `+0x38` are part of a pointer (the write crashed the game), and the size-like values at `+0xf8`, `+0x1b8`, `+0x278` are measured text widths. Nothing there is a settable pane width.
- The widget behind `rect_TextTitle` is not a plain pane. Its virtual functions (first words of the table at `main+0x1c8e4b8`) are an animation and state wrapper, so a move or resize needs the layout's own calls, which were not needed once the spare panes were found.
- Making the name look like the title text (larger and brighter): the spare panes use the layout's sub-title text style, which is smaller and grayer. `[ST:col ...]` colour tags with parameter names `p1`, `ColorID`, `id` and `name` and values 0 to 9 changed nothing (the tags are accepted and the text kept), and calling the widget's scale function at `main+0x917448` with the vector registers set to 1.0 and 1.35 changed nothing either. The size, weight and colour are fixed by the layout.
- Letting the title's own text carry the name (it would match the title's font exactly): the title's text object (`[widget+0x38]`) has an overflow mode at `+0x3f0` (a setter is `main+0xa1d204`, called with `widget+0x38`), and the game sets it to 3, which shrinks the whole text to the pane's width. Mode 2 also shrinks it, modes 0, 4 and 5 clip the text at the pane's right edge (the R1 icon after the page label disappears), mode 1 draws it small and high. A width field at `+0x3da` of the text object reads 172 (the pane's width) and writing 900 to it, before or after the text is set, changes nothing: the clip comes from the layout done when the text is set. So a text that is wider than the title pane cannot be shown in it.
- Mouse clicks on the title: the cursor is captured and hidden on this screen, and left click is already Y, so Y covers the click as well.

## How the frame's layout is read

A layout handle in the game is a pair: the layout object and a part index, which is what `scene+0xd8+0x10*row` and the frame's handle hold. `main+0x55f16c` finds a child by name through `main+0x915d00`, where the name is a `/` separated path into the layout's pane tree; the result is a pane object, `main+0x915110` takes it as a text pane (or returns 0), `main+0x91720c` sets its text from UTF-8 and the virtual at `+0x20` shows or hides it. The by-message-id setter at `main+0x55f548` (handle, child name, message) is how the frame sets its title; hooking its entry gives the frame's handle, and a loop of find-child calls over a list of names (`python3 grepstr.py` style rodata strings starting with `rect_`) lists the panes that layout really has. That is the quickest way to find a spare pane for text.

## Limits

- 30 manual slots (`SAVE_PAGE_COUNT` in `runtime/save_pages.h`). The file names allow more; the screen shows three at a time, so a higher count only adds pages.
- Each slot is about 0.9 MiB (the save file and its thumbnail), so 30 slots add about 27 MiB. A save commit links unchanged files; a commit with 19 files took 51 ms on Linux.
- The screen's own arrays hold four rows, so the page window is what makes more slots possible without editing the game's layout.

## Checks

- `tests/save_pages_check.py` runs the page range, the name mapping, the label and the page names (cleaning, limit, file format; no game files needed).
- `python3 recomp/gen.py` stops with a message when an instruction word at a site differs.
- By hand with a profile that has saves: open Load Game on the title screen, change pages with L and R, rename a page with Y, save from the in-game menu on a later page, and load a slot from it.
- Without touching the controller: [ui-testing.md](ui-testing.md) shows how the Linux checks were done with scripted presses and screenshots (page change, rename, controller icon styles).

The game's button bits in the mask the Save/Load screen reports (`rt_save_pages_pad`): Y `0x01`, L `0x10`, R `0x20`, ZL `0x40`, ZR `0x80`, Minus `0x200`. They were found by pressing each button by script and logging the mask, not from the Switch's own order. A Y press is reported again on the frame the text entry closes, so `rt_save_pages_pad` ignores Y for 30 hook calls after one.
