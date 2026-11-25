// The fit of single hairstyles, recorded by hand with the Modify window and taken over as the starting point of each hairstyle (the window then
// shows them as scale 1.0 and no shift or tilt). One entry per hairstyle: key = row * 2 + 1 when the wearer is female, else row * 2; v[part][value]
// with the parts in the order of MG_* (whole hair "All", scalp cap "Main", front, sides, tails, other strands) and the values scale change (0.05 = x1.05),
// left/right, up/down, back/forward (metres) and tilt (degrees). The record of a creator session is written to <profile>/hair-modify-record.txt
// (docs/character-creator.md, "Modify: fitting a hair by hand"). The list ends with the key 0xffffffff.
//
// "faces" is the set of creator faces an entry applies to, one bit per face (0x7 = the male faces 0 to 2, presets 1 to 6). The first entry of a hairstyle whose set has the
// wearer's face is used, so a refinement made on another face goes above the earlier entry of the hairstyle. A hairstyle that is one of the face's own eight (rows
// r0 to r0 + 7 of the face) never gets start values (mod_load_baked).
//
// These are the male hairs fitted by hand on the first male face (2026-11-24, the last line of each hairstyle in the record), for the faces 0 to 2.
static const struct { uint32_t key; uint16_t faces; double v[MG_N][MP_N]; } mod_baked[] = {
    // refined on the faces 1 and 2 (presets 3 to 6), above the entries of the faces 0 to 2 below it
    {2, 0x6, {{0.04, 0, 0, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 1 (grid row 1, item 1): made for face 0, a little larger on face 1
    {4, 0x6, {{0.04, 0, 0, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 2 (grid row 1, item 2): the same
    {44, 0x6, {{0.04, 0, -0.014, 0, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 22 (grid row 6, item 2), refined on presets 3 and 4
    {46, 0x6, {{0.03, 0, -0.006, -0.002, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 23 (grid row 6, item 3), refined on presets 3 and 4
    {48, 0x6, {{0.05, 0, -0.012, 0.002, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 24 (grid row 6, item 4), refined on presets 3 and 4
    {52, 0x6, {{0.04, 0, -0.018, 0.01, 4}, {0}, {0}, {0}, {0}, {0}}},   // hair 26 (grid row 7, item 2), refined on presets 3 and 4
    {56, 0x6, {{0.0404, 0, -0.016, 0.006, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 28 (grid row 7, item 4), refined on presets 3 and 4
    {60, 0x6, {{0.04, 0, -0.014, 0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 30 (grid row 8, item 2), refined on presets 3 and 4
    {62, 0x6, {{0.02, 0, -0.016, 0.006, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 31 (grid row 8, item 3), refined on presets 3 and 4
    {64, 0x6, {{0.0547, 0, -0.016, 0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 32 (grid row 8, item 4), refined on presets 3 and 4
    {66, 0x6, {{0.1124, 0, -0.01, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 33 (grid row 9, item 1), refined on presets 3 and 4
    {72, 0x6, {{0.0403, 0, -0.018, 0.006, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 36 (grid row 9, item 4), refined on presets 3 and 4
    {76, 0x6, {{0.05, 0, -0.01, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 38 (grid row 10, item 2), refined on presets 3 and 4
    {80, 0x6, {{0.03, 0, -0.014, 0.004, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 40 (grid row 10, item 4), refined on presets 3 and 4
    {82, 0x6, {{0.1948, 0, -0.014, 0.004, -1}, {0}, {0}, {0, 0, 0, -0.002, 0}, {0}, {-0.25, 0, 0, 0, 0}}},   // hair 41 (grid row 11, item 1), refined on presets 3 and 4
    {84, 0x6, {{0.0918, 0, -0.01, 0.012, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 42 (grid row 11, item 2), refined on presets 3 and 4
    {88, 0x6, {{0.1128, 0, -0.012, 0.004, -2}, {0}, {0}, {0}, {0}, {0}}},   // hair 44 (grid row 11, item 4), refined on presets 3 and 4
    {90, 0x6, {{0.1336, 0, -0.014, 0.01, -1}, {0}, {0}, {0}, {0}, {0}}},   // hair 45 (grid row 12, item 1), refined on presets 3 and 4
    {92, 0x6, {{0.113, 0, -0.012, 0.004, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 46 (grid row 12, item 2), refined on presets 3 and 4
    {96, 0x6, {{0.0608, 0, -0.016, 0.008, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 48 (grid row 12, item 4), refined on presets 3 and 4
    {98, 0x6, {{0.1628, 0, 0.002, 0.012, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 49 (grid row 13, item 1), refined on presets 3 and 4
    {102, 0x6, {{0.144, 0.002, -0.002, 0.008, 5}, {0}, {0}, {0}, {0}, {0}}},   // hair 51 (grid row 13, item 3), refined on presets 3 and 4
    {104, 0x6, {{0.1227, 0, 0, 0.01, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 52 (grid row 13, item 4), refined on presets 3 and 4
    {106, 0x6, {{0.1752, 0, 0, 0.004, 1}, {0}, {0}, {0}, {0}, {0}}},   // hair 53 (grid row 14, item 1), refined on presets 3 and 4
    {108, 0x6, {{0.1856, 0, 0.002, -0.002, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 54 (grid row 14, item 2), refined on presets 3 and 4
    {110, 0x6, {{0.1648, 0, 0.008, 0.002, 4}, {0}, {0}, {0}, {0}, {0}}},   // hair 55 (grid row 14, item 3), refined on presets 3 and 4
    {18, 0x7, {{0, 0, -0.01, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 9 (grid row 3, item 1), male wearer
    {20, 0x7, {{0, 0, -0.002, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 10 (grid row 3, item 2), male wearer
    {22, 0x7, {{0, 0, -0.008, 0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 11 (grid row 3, item 3), male wearer
    {24, 0x7, {{0, 0, -0.006, -0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 12 (grid row 3, item 4), male wearer
    {26, 0x7, {{0.01, 0, -0.004, -0.004, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 13 (grid row 4, item 1), male wearer
    {28, 0x7, {{0.03, 0, -0.008, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 14 (grid row 4, item 2), male wearer
    {32, 0x7, {{0, 0, -0.004, -0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 16 (grid row 4, item 4), male wearer
    {34, 0x7, {{0, 0, 0, -0.002, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 17 (grid row 5, item 1), male wearer
    {36, 0x7, {{0, 0, -0.012, 0, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 18 (grid row 5, item 2), male wearer
    {38, 0x7, {{0, 0, -0.01, 0, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 19 (grid row 5, item 3), male wearer
    {40, 0x7, {{0, 0, -0.006, -0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 20 (grid row 5, item 4), male wearer
    {42, 0x7, {{0, 0, -0.012, -0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 21 (grid row 6, item 1), male wearer
    {44, 0x7, {{0, 0, -0.014, 0, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 22 (grid row 6, item 2), male wearer
    {46, 0x7, {{0, 0, -0.006, -0.002, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 23 (grid row 6, item 3), male wearer
    {48, 0x7, {{0, 0, -0.014, 0, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 24 (grid row 6, item 4), male wearer
    {50, 0x7, {{0.01, 0, -0.01, 0.004, 1}, {0}, {0}, {0}, {0}, {0}}},   // hair 25 (grid row 7, item 1), male wearer
    {52, 0x7, {{0, 0, -0.018, 0.01, 4}, {0}, {0}, {0}, {0}, {0}}},   // hair 26 (grid row 7, item 2), male wearer
    {54, 0x7, {{0.02, 0, -0.016, 0.006, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 27 (grid row 7, item 3), male wearer
    {56, 0x7, {{0.02, 0, -0.016, 0.006, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 28 (grid row 7, item 4), male wearer
    {58, 0x7, {{0, 0, -0.016, 0.006, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 29 (grid row 8, item 1), male wearer
    {60, 0x7, {{0, 0, -0.014, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 30 (grid row 8, item 2), male wearer
    {62, 0x7, {{0, 0, -0.016, 0.006, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 31 (grid row 8, item 3), male wearer
    {64, 0x7, {{0.024, 0, -0.016, 0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 32 (grid row 8, item 4), male wearer
    {66, 0x7, {{0.08, 0, -0.01, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 33 (grid row 9, item 1), male wearer
    {70, 0x7, {{0.02, 0, -0.016, 0.006, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 35 (grid row 9, item 3), male wearer
    {72, 0x7, {{0.01, 0, -0.018, 0.006, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 36 (grid row 9, item 4), male wearer
    {74, 0x7, {{0.03, 0, -0.016, 0.002, -2}, {0}, {0}, {0}, {0}, {0}}},   // hair 37 (grid row 10, item 1), male wearer
    {76, 0x7, {{0, 0, -0.01, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 38 (grid row 10, item 2), male wearer
    {78, 0x7, {{0.01, 0, -0.006, 0.004, 1}, {0}, {0}, {0}, {0}, {0}}},   // hair 39 (grid row 10, item 3), male wearer
    {80, 0x7, {{0, 0, -0.014, 0.004, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 40 (grid row 10, item 4), male wearer
    {82, 0x7, {{0.16, 0, -0.014, 0.004, -1}, {0}, {0}, {0, 0, 0, -0.002, 0}, {0}, {-0.25, 0, 0, 0, 0}}},   // hair 41 (grid row 11, item 1), male wearer
    {84, 0x7, {{0.06, 0, -0.01, 0.012, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 42 (grid row 11, item 2), male wearer
    {86, 0x7, {{0.07, 0, -0.006, 0.004, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 43 (grid row 11, item 3), male wearer
    {88, 0x7, {{0.04, 0, -0.012, 0.004, -2}, {0}, {0}, {0}, {0}, {0}}},   // hair 44 (grid row 11, item 4), male wearer
    {90, 0x7, {{0.09, 0, -0.014, 0.01, -1}, {0}, {0}, {0}, {0}, {0}}},   // hair 45 (grid row 12, item 1), male wearer
    {92, 0x7, {{0.05, 0, -0.012, 0.006, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 46 (grid row 12, item 2), male wearer
    {96, 0x7, {{0.02, 0, -0.016, 0.006, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 48 (grid row 12, item 4), male wearer
    {98, 0x7, {{0.14, 0, 0.002, 0.012, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 49 (grid row 13, item 1), male wearer
    {100, 0x7, {{0.13, 0, 0, 0.004, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 50 (grid row 13, item 2), male wearer
    {102, 0x7, {{0.1, 0.002, -0.002, 0.008, 5}, {0}, {0}, {0}, {0}, {0}}},   // hair 51 (grid row 13, item 3), male wearer
    {104, 0x7, {{0.03, 0, 0, 0.008, 2}, {0}, {0}, {0}, {0}, {0}}},   // hair 52 (grid row 13, item 4), male wearer
    {106, 0x7, {{0.13, 0, 0, 0.004, 1}, {0}, {0}, {0}, {0}, {0}}},   // hair 53 (grid row 14, item 1), male wearer
    {108, 0x7, {{0.14, 0, 0.002, -0.002, 3}, {0}, {0}, {0}, {0}, {0}}},   // hair 54 (grid row 14, item 2), male wearer
    {110, 0x7, {{0.12, 0, 0.008, 0.004, 4}, {0}, {0}, {0}, {0}, {0}}},   // hair 55 (grid row 14, item 3), male wearer
    {268, 0x7, {{0, 0, -0.008, 0, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 134 (grid row 15, item 1), male wearer
    {270, 0x7, {{0.03, 0, -0.006, -0.002, 0}, {0}, {0}, {0}, {0}, {0}}},   // hair 135 (grid row 15, item 2), male wearer
    {272, 0x7, {{-0.02, 0, -0.022, 0, -2}, {0}, {0}, {0}, {0}, {0}}},   // hair 136 (grid row 15, item 3), male wearer
    {0xffffffffu, 0, {{0}}},
};
