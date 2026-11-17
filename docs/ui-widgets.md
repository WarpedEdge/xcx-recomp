# Changing the game's on-screen text and menus from a hook

How to find and change text on a game screen, play a menu sound, read the buttons a screen sees and open the host's text entry, from the hooks in `recomp/*.py` and the runtime code they call. Everything here was worked out on the 2.0.0 `main` while adding [save slot pages](save-slots.md), and the offsets are `main` offsets from `main+0x0`. [ui-testing.md](ui-testing.md) shows how to drive the game and look at the result.

If a screen needs something this page does not cover, find it out and add a section here (or a new doc for a separate topic), so the next change starts from it.

## Hook rules

- Hooks are emitted by the generator (`recomp/<feature>.py`) only when the build ID, the hash and the instruction words at every site match. Another `main` is lifted without them.
- An entry hook (at the first instruction of a function) may call guest functions with `rt_call(c, fn, n, ...)`. A hook in the middle of a function may only read and write memory or values: the function's registers live in C locals there, so a call would work on stale registers.
- `rt_call` passes integer registers only. A float argument goes in `c->v[i]` (`__uint128_t v[32]`) before the call.
- A hook that calls the guest saves `c->x[0..18]` first and puts them back after, so the function it interrupted carries on with its own registers.
- Guest addresses are host pointers: `LD8/16/32/64` read them. Module offsets are added to `MAIN = 0x7000000000`.

## Finding a widget

A layout handle in the game is a pair: the layout object and a part index (`scene+0xd8+0x10*row` holds one per row).

| Function | Use |
| --- | --- |
| `main+0x55f16c` | find a child of a layout handle by name |
| `main+0x915d00` | what the find-child call uses: the name is a `/` separated path into the layout's pane tree |
| `main+0x915110` | casts a pane to a text pane (returns 0 if it is not one) |
| `main+0x91720c` | sets a text pane's text from UTF-8 |
| the virtual at `+0x20` of a pane | shows or hides it |
| `main+0x55f548` | sets a label by message id: `(handle, child name, message)`. Titles go through it |

To get the handle of a screen's frame without knowing the screen's fields, hook the entry of `main+0x55f548`: its first argument is the handle of the layout whose label is being set. To list the panes a layout really has, call find-child in a loop over the `rect_` strings found in the main module's rodata and keep the ones that answer.

## Setting text

- Text set with `main+0x91720c` or by message id goes through the game's own text layouter, so it uses the game's font and the tags below. The game's strings are English in every language here.
- `[ST:icon p1=l]` is a controller button icon. It becomes `[ML:icon ...]` before it is drawn and follows the Button Icon Type setting, also with no controller connected. How the icon route works is in [glyph-inline-text-route.md](glyph-inline-text-route.md) and [glyph-inline-trial.md](glyph-inline-trial.md). Use the tag instead of writing a button name.
- Other tags in the game's text: `ST:col` (becomes `System:Color`), `ST:space`, `ST:wide`, `ST:wait`, `ST:page`, `ST:undisp`, `ST:ruby`, `ST:playername`, `ST:gender`. Text that comes from a player (a name) has `[` and `]` removed before it is set, so it cannot start a tag.
- A text pane squeezes or clips its text to its own width, and the layout fixes its size, weight and colour. See the dead ends below.

### The title bar of the Save and Load screens

The frame layout of those screens has a title pane (`rect_TextTitle`) that squeezes its text to its width, which ends at the small slash. It also has two panes the screens leave empty: `rect_TextTitle02` (sits lower than the title line) and `rect_TextTitle03` (starts where `02`'s text ends). Text in `03` lands in the empty box right of the slash, at the title's height; blank spaces in `02` move it right. They were found by looking up about 1000 pane names from the rodata in the frame's layout.

- With no spaces the text in `03` starts at pixel 355 (at 1920 wide). One space is about 9.1 px, measured from screenshots with 20 and 40 spaces. The centre of the box is about 1018.
- The font is proportional, so a width is an estimate: `name_spacer` in `runtime/save_pages.c` sums a width per kind of character (narrow, normal, wide, full-width) and sets the blanks to put the text's middle at the box's middle. It is good to a few characters, not exact.
- The panes use the layout's sub-title text style, which is smaller and grayer than the title.

## Playing a menu sound

`main+0x208554` returns the sound manager. `main+0x40bc48(manager, id, 0)` plays an effect. Effect 5 is the one the game's own screens play when L or R switches a tab or a page. Call it from an entry hook, not from the middle of a function: `save_pages.c` asks for the sound in the pad hook and plays it from the screen's update entry on the next frame.

## Buttons a screen sees

A screen's update gets a mask of the buttons pressed this frame. On the Save and Load screen: Y `0x01`, L `0x10`, R `0x20`, ZL `0x40`, ZR `0x80`, Minus `0x200`. They are not the Switch's own order. They were found by pressing each button from an input script and logging the mask with a temporary line in the hook (see [ui-testing.md](ui-testing.md)); another screen may differ, so check it the same way.

A press that opens a text entry is reported again on the frame the entry closes, so ignore it for a short while after (`rename_cooldown` in `save_pages.c`: 30 hook calls).

## Asking the player for text

`text_entry_run` (`runtime/swkbd.h`) shows the host's text entry, the one the game's character naming uses (see [naming-and-controller-keyboard.md](naming-and-controller-keyboard.md); its keyboard works with a controller). From a guest thread, give up the virtual core and block the game's input while it waits: `kern_core_away`, `input_block(1)`, then `input_block(0)` and `kern_core_back`, as `rename_page` in `save_pages.c` does. `swkbd_utf8_to_utf16` and `swkbd_utf16_to_utf8` convert the text. For a test, `XCX_SWKBD_TEST=type:<text>` types the text without a window.

Settings the test tools take from the environment (`XCX_*`) are read with `cfg_get` from `runtime/cfg.h`; `getenv` does not see them.

## What did not work

Collected so nobody repeats it. All of it was tried on the title text of the Save and Load screens, to make the page name look like the title.

- Writing into the title widget's fields: the fields near `+0x38` are part of a pointer (the write crashed the game), and the size-like values at `+0xf8`, `+0x1b8`, `+0x278` are measured text widths. Nothing there is a settable pane width.
- The widget behind `rect_TextTitle` is not a plain pane. Its virtual functions (first words of the table at `main+0x1c8e4b8`) are an animation and state wrapper, so a move or resize would need the layout's own calls.
- Colour tags: `[ST:col ...]` with parameter names `p1`, `ColorID`, `id` and `name` and values 0 to 9 changed nothing (the tags are accepted and the text kept).
- The scale function `main+0x917448(widget, s0, s1)` with the vector registers set to 1.0 and 1.35 changed nothing.
- Letting the title's own text carry the name: the title's text object (`[widget+0x38]`) has an overflow mode at `+0x3f0` (setter `main+0xa1d204(widget+0x38, mode)`). The game sets it to 3, which shrinks the whole text to the pane's width. Mode 2 also shrinks it, modes 0, 4 and 5 clip the text at the pane's right edge, mode 1 draws it small and high. A width field at `+0x3da` of the text object reads 172 (the pane's width) and writing 900 to it, before or after the text is set, changes nothing: the clip comes from the layout done when the text is set.
- Copying style-looking fields from the title's text object onto the name pane's: the colour-like floats, the effect values, the style ids and the glyph block changed nothing on screen, and the structural fields (`+0xd4`, `+0xf0`, `+0x1e4`) crashed the game. The title's text object holds the style, so a style setter that takes the title's style id is the thing still to find.
- A font name: no font family name (Futura or another) could be read from the game's data. Only `MNU_Font*`, `MNU_ResFontStyle` and `MNU_FONTICON` strings turned up. The title and the name pane both use the game's own menu font; they differ in size, weight and colour, which the layout sets per pane.
- Mouse clicks on the title: the cursor is captured and hidden on this screen, and the left click is already Y by default, so Y covers the click.
