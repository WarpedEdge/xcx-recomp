# Reading the game's data archive and tables

How to find a value the game keeps in its data, such as a list of hairstyles, without the game running. It was worked out while looking at the [character creator](character-creator.md). All offsets are `main` offsets (from `main+0x0`) in the 2.0.0 build.

Status: checked on Linux against the 2.0.0 RomFS. Nothing here was run on Windows. The reader scripts used for this are not in the repository.

## The archive

Almost all game data is in `sts.arh` and `sts.ard`, not in loose RomFS files. The RomFS index lists only movies, sound packs and these two.

- `sts.arh` starts with the tag `arh2`, then a 32-bit member count (104,824), then 8 more bytes. After a 16-byte header come 16-byte records, one per member, in file order: a 64-bit `xxh64` of the path (seed 0, with the leading slash), the member's size in `sts.ard`, and its unpacked size.
- A member starts where the previous one ended, rounded up to 16 bytes. The first starts at 0.
- An unpacked size of 0 means the member is stored as it is. Otherwise it starts with a 0x30 byte `xbc1` header (type 3 is zstd, then unpacked size, packed size, hash, name) and the zstd data follows.
- Member names are not stored. A name is found by hashing a guess, so a table is found by its content. A member whose content starts with `BDAT` is a data file.

The member with the creator's tables is 9,968,848 bytes and holds 704 tables. The Seamless notes call it `bdat-common.bdat`.

## BDAT files and tables

- A file starts with `BDAT`, the word `0x01001004`, a table count, the file size, then one offset per table.
- A table starts with `BDAT`, the word `0x3004`, and these 32-bit values: column count, row count, first row id, 0, then the offsets (from the start of the table) of the column definitions, the row hash list, the rows and the string pool, the row length and the string pool length.
- A column definition is a type byte and a 16-bit offset into the string pool. Types seen: 1 `u8`, 2 `u16`, 3 `u32`, 4 `s8`, 5 `s16`, 6 `s32`, 7 string (a 32-bit offset into the pool), 8 `f32`, 9 hash (`u32`). Types 10 to 13 are 1 to 2 bytes. Rows are packed in column order. The row length matches the column sizes in 696 of the 704 tables. The other 8 use types 10 to 13, whose sizes are still a guess.
- Names are hashes. The table name and each column name are 32-bit Murmur3 values (seed 0) of the plain name, kept in the string pool: the table name at pool offset 1, the columns at the offset in their definition. The same names are also stored as plain text in a name list in front of the column definitions, which is how the names were recovered.
- Row ids start at the table's first row id (1 for the creator tables, 0 for the character list).

## How the game reads a table

- `main+0x3dd6e0(hash)` looks a table up by its name hash in a list at `0x7002705c80` and returns its handle.
- Small accessor functions wrap the lookups. Each loads the table hash and the column hash as 32-bit constants built from two instructions (a low half, then a high half), and calls `0x3ddbcc` for a column value, `0x3ddf34` for the row count or `0x3ddf3c` for the first row id.
- To find who reads a table, hash its name, then search the generated C for the low half followed by the high half in the same register. A string in the main module with no code reference is a name used only through hashes.

## Checks

- `xxh64` of the empty string is `0xef46db3751d8e999`.
- `Murmur3("test")` with seed 0 is `0xba6bd213`.
- Offsets at 16 byte alignment gave `xbc1` or a raw `BDAT` at the start of every member tried.
