// Extra hairstyles in the character creator's list (docs/character-creator.md; the game side is recomp/avatar_hair.py).
#pragma once
#include <stdint.h>
#include "cpu.h"

// Called at the entry of the creator's list builder with the base face the list is built for.
void avatar_hair_face(uint64_t self, uint64_t face);
uint64_t avatar_hair_items(uint64_t ui, uint64_t index);
void avatar_hair_setcursor(Cpu *c);
int avatar_hair_hige(Cpu *c);
int avatar_hair_hint(int *page, int *male, int *source, int *family, int *symbol);
int avatar_hair_tick(Cpu *c);
int avatar_hair_hud(char *buf, int n);
int avatar_hair_modify_ui(char lines[48][72], int *open, int *sel);   // the window of the Modify row (runtime/ui/menu.cpp draws it)
int avatar_hair_listbar(float *pos, float *len);
int avatar_hair_dialog(void);
void avatar_hair_snd(Cpu *c);   // the category list's scroll bar while it shows 7 of 8 rows
const char *avatar_hair_shot_tag(void);   // test: name of a wanted overlay shot, once
uint64_t avatar_hair_cats(uint64_t ui, uint64_t cursor);   // after the hair tab's category list is built: adds the Modify row, returns the cursor for the list widget
int avatar_hair_text(Cpu *c);
void avatar_hair_ibind(Cpu *c, uint64_t self, uint64_t elem);
void avatar_hair_pose(Cpu *c, uint64_t sp, uint64_t slot, uint64_t wrap, uint64_t arg);

// Called at the entry of the AvtChrID reader (x0 = the row handle). 1 when the row is an extra of the current face: x0 is then the
// face, as if the row belonged to it, and the reader must return at once; 0 leaves the reader to run.
int avatar_hair_avt(Cpu *c);

// The builder's icon answer: an IconID of 0 (a hairstyle with no icon) becomes a placeholder icon.
uint64_t avatar_hair_icon(uint64_t icon);
int avatar_hair_iconid(Cpu *c);

// Solid-colour icons for the hairstyles that have none (runtime/vk_texture.c calls these). avatar_icon_patch is called
// for every level of every uploaded texture right after its block-linear deswizzle (dst: rows block rows of rb bytes). On
// a hair icon sheet it paints the cells that stand in for the story characters' hairstyles; the result is the overlay
// generation plus 1 at level 0 of a sheet, else 0. avatar_icon_gen changes when the overlay does, and a texture painted
// with an older one is uploaded again.
uint32_t avatar_icon_patch(uint32_t w, uint32_t h, int bpb, int bw, unsigned level, uint8_t *dst, uint32_t rb, uint32_t rows);
uint32_t avatar_icon_gen(void);

