"""Extra hairstyles in the character creator's list, for the verified XCX DE 2.0.0 main (docs/character-creator.md).

The creator lists the rows of ITM_AvatarHairList whose AvtChrID (a column read by main+0x54674c) equals the current
base face. Every caller of that reader compares the result with the face. The builder of the list is main+0x7799b0
(face in x1). The hooks:
  main+0x7799b0  entry: remember the face the list is built for
  main+0x54674c  entry: for a row that is an extra of that face, answer with the face, so the row passes the compare
  main+0x546798  entry: the IconID reader; for a story character's row, answer with an icon borrowed from a creator hairstyle
  main+0x76de70  after its first instruction (the grid's item loop is done; w1 is the current hairstyle's position): move the story
                 characters' items to the end and give the position they leave the current hairstyle at
  main+0x5461e8  entry: the reader of the base face's HigeType (the beard class that decides whether the Mustache and Beard
                 categories show); for the creator's own category list (return address main+0x76d814) a male face answers 2, so
                 every male face shows both
  main+0x5ac570  entry: the grid widget's "set the cursor to an item" (grid in x0, item in w1, scroll flag in w2); for the creator's
                 grid a flag of 0 becomes 1, so an item below the first page scrolls into view (the game's lists fit one page)
  main+0x317a60  after the last store of one skin bone's pose matrix (per-frame pose feed main+0x3177a0): fit a part's bind data to the
                 wearer's own skeleton (runtime/avatar_hair.c, avatar_hair_pose)
  main+0x9bca1c  after the palette builder's last load of a bone's inverse bind matrix: while the creator is open, replace it by the
                 wearer's own skeleton's (runtime/avatar_hair.c, avatar_hair_ibind)
  main+0x779a4c  after `and w0, w0, #0xffff`: the builder's IconID answer; 0 (a row with no icon) becomes a placeholder
  main+0x768d50  entry: the creator UI's per-frame update (UI object in x0); a page change waiting runs the hair tab's entry routine
                 (main+0x76d45c) on the game's thread, so the grid is built again without leaving the tab (runtime/avatar_hair.c,
                 avatar_hair_tick)
  main+0x76dd54  after `ldr w1, [x27, #0x708]` (the hair tab's category list is built; the list widget's layout call follows): append the
                 Modify row to the category items (runtime/avatar_hair.c, avatar_hair_cats)
  main+0x917160  entry: the text setter that takes a text table and a message id (the Modify row's label is our own text; the hook only
                 looks at it, runtime/avatar_hair.c, avatar_hair_text)
  main+0x40bc48  entry: the game's "play sound" (sound manager in x0, id in w1): while the hair tab is open its calls are logged
                 (runtime/avatar_hair.c, avatar_hair_snd), so the creator's own sounds can be named
The rows come from runtime/avatar_hair.c.
"""

BUILD = 'ec4ed46bbc4a9ebbe7ce0af7a4a8ebbc09e5724a'
SHA256 = '0a9dbf1f3816dd0fab0a08883d538c74d9b9fd7d939cb29aaf0947d71a201026'
GETTER = 0x54674c
BUILDER = 0x7799b0
ICON_MASK = 0x779a4c
ICONID = 0x546798
ITEMS_DONE = 0x76de70
SETCURSOR = 0x5ac570
HIGE = 0x5461e8
HIGE_CALLER = 0x76d814
POSE = 0x317a60
TICK = 0x768d50
IBIND = 0x9bca1c
CATS = 0x76dd54
TEXT = 0x917160
SND = 0x40bc48
WORDS = {GETTER: 0xb9400008,      # ldr w8, [x0]: the row id of the handle in x0
         ICONID: 0xb9400008,      # the same, in the IconID reader
         ITEMS_DONE: 0xb9470e61,  # ldr w1, [x19, #0x70c]: the grid's items are built; the current hairstyle's position goes to the widget
         HIGE: 0xb9400008,        # ldr w8, [x0]: the row id of the handle in x0
         SETCURSOR: 0xa9bd7bfd,   # stp x29, x30, [sp, #-48]!
         SND: 0xf943f409,         # ldr x9, [x0, #0x7e8]: the sound queue's count, the first instruction of the game's "play sound"
         CATS: 0xb9470b61,        # ldr w1, [x27, #0x708]: the category cursor, the last instruction before the list widget's layout call
         IBIND: 0x3cc68126,       # ldur q6, [x9, #104]: the last load of a bone's inverse bind matrix in the palette builder
         POSE: 0xbd00bbe0,        # str s0, [sp, #0xb8]: the pose matrix (sp+0x80) of one skin bone is complete; bl setter follows
         TICK: 0xa9be7bfd,        # stp x29, x30, [sp, #-32]!: the UI update's first instruction
         BUILDER: 0xd101c3ff,     # sub sp, sp, #0x70
         ICON_MASK: 0x12003c00}   # and w0, w0, #0xffff


def verified(mod):
    if mod.name != 'main' or mod.nso.build_id != BUILD:
        return False
    if mod.nso.source_sha256 != SHA256 or mod.base != 0x7000000000:
        raise ValueError('Avatar hair: build identity mismatch')
    for offset, word in WORDS.items():
        if mod.word(mod.base + offset) != word:
            raise ValueError(f'Avatar hair: instruction mismatch at main+{offset:#x}')
    return True


def entry(mod, address):
    """C at the first instruction of the function at address, or ''."""
    offset = address - mod.base
    if offset == GETTER:
        return 'if (avatar_hair_avt(c)) return;'
    if offset == ICONID:
        return 'if (avatar_hair_iconid(c)) return;'
    if offset == BUILDER:
        return 'avatar_hair_face(c->x[0], c->x[1]);'
    if offset == HIGE:
        return 'if (avatar_hair_hige(c)) return;'
    if offset == SETCURSOR:
        return 'avatar_hair_setcursor(c);'
    if offset == TICK:
        return 'avatar_hair_tick(c);'
    if offset == TEXT:
        return 'if (avatar_hair_text(c)) return;'
    if offset == SND:
        return 'avatar_hair_snd(c);'
    return ''


def after(mod, address):
    """C to run after the instruction at address, or None."""
    offset = address - mod.base
    if offset == ICON_MASK:
        return 'c->x[0] = avatar_hair_icon(c->x[0]);'
    if offset == IBIND:
        return 'avatar_hair_ibind(c, x19, x9);'
    if offset == POSE:
        return 'avatar_hair_pose(c, sp_, x21, x0, x1);'
    if offset == CATS:
        return 'c->x[1] = avatar_hair_cats(x27, c->x[1]);'
    if offset == ITEMS_DONE:
        return 'c->x[1] = avatar_hair_items(c->x[19], c->x[1]);'
    return None
