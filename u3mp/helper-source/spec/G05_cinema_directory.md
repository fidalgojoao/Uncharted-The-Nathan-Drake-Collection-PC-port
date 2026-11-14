# G05. Cinema: film directory, scan, select / protect / delete, progress pages

Behaviour description for a reimplementation (reimpl2 pipeline, describer's document). It says what the functions
do, not how the old ones were written. Source of the facts: helper v7.212, six functions of the C part (`cin_mkdirs`,
`cin_stream_init`, `cin_netinit`, `cin_scan`, `cin_flags`, `cin_scan_free`) and seven of the C++ part
(`u3x_rfc2822`, `u3x_film_select`, `u3x_film_protect`, `u3x_film_open`, `u3x_film_progress_page`,
`u3x_film_toggle_list`, `u3x_cinema`), together 202 code lines, and all their callers.

Conventions of this document: offsets are hexadecimal when prefixed with 0x. "Image offset X" is the address
`base + X`, `base` being the game image base. All multi-byte values are little-endian. "u8 / u16 / u32 / u64 / s16 /
s32 / s64" are unsigned / signed integers of that width; "pointer" is 8 bytes. "Text" is a NUL-terminated byte
string. Nothing in this group uses floating point.

## 1. Purpose and callers

The PS4 build of the game still contains the film recorder and the film player ("Cinema"), but lost what the PS3
build does around them: creating the film directory, building the list of recorded films, choosing a film, the
Protect / Unprotect buttons, freeing a slot before a recording, the "Refresh Cinema files" progress page, the
local / online list toggle and the Cinema entry of the lobby. This group supplies those parts.

| Function | Called by | When |
|---|---|---|
| film set-up (old C `cin_netinit`) | C export `u3h_cin_netinit`, which is called by the C++ part's NetInit step (once per NetInit, after the player's online id has been written) and by the C++ part's "set-up on first use" guard (used by `u3x_film_open`, `u3x_cinema` and the not-in-this-group `u3x_film_prepare`) | network start; first Cinema use |
| recursive directory creation (old C `cin_mkdirs`) | the film set-up only | |
| stream defaults (old C `cin_stream_init`) | the film set-up only, twice | |
| film scan (old C `cin_scan`) | C export `u3h_cin_scan`: used by `u3x_cinema`, by the list refresh of `u3x_film_toggle_list`, by the scan-and-free function, and by `u3x_film_delete` (other group) | Cinema entry, list toggle, before a recording, after a delete |
| button flags (old C `cin_flags`) | C export `u3h_cin_flags`: used by `u3x_film_select`, `u3x_film_protect`, `u3x_film_progress_page`, and `u3x_film_delete` (other group) | whenever the selected film or its state changed |
| scan and free a slot (old C `cin_scan_free`) | C export `u3h_cin_scan_free`: used by `u3x_film_open` only | before a recording |
| `u3x_rfc2822` | C wrapper `cin_rfc2822` (date text of a film; used by the scan) | once per file seen by a scan |
| `u3x_film_select` | `u3x_cinema` (film 0); `u3x_cinema_rows` (group G06; list operation 10 "row picked" on the local list); C wrapper `cin_select` (no user in v7.212) | Cinema entry; the player picks a film |
| `u3x_film_protect` | C wrapper `cin_protect`, from the stub dispatcher `cinema_kind`: stub kind 169 with `on` = 1 (Protect button), kind 170 with `on` = 0 (Unprotect button) | button press |
| `u3x_film_open` | C wrapper `cin_open_hook`, stub kind 70: a value stub at the entry of the game's film writer Open (image 0x6a2760) | every Open of a film stream |
| `u3x_film_progress_page` | C wrapper `cin_progress_page`, stub kind 72: value stub at the game's (empty) page handler of the progress pages (image 0x6648a0) | every event sent to the progress page |
| `u3x_film_toggle_list` | C wrapper `cin_toggle_list`, stub kind 73: value stub at the game's (empty) handler of the local / online toggle button (image 0x665400) | button press |
| `u3x_cinema` | C wrapper `cinema` (catch-all menu action 0xb85cb12a) and `u3x_lobby_row` (C++, lobby row action 0xb85cb12a) | the player chooses Cinema in the lobby |

The stub dispatcher hands a value stub an array `r` of the saved 64-bit registers of the interrupted game function:
`r[4]` is its first argument register (rdi), `r[5]` its second (rsi). The integer a stub function returns tells the
stub whether the game's own body runs afterwards (0 = it runs); the values given in section 4 are to be returned
exactly.

## 2. Interface

No function takes or returns a float. The seven `u3x_` functions that exist keep their names and signatures. The
six C functions get new `u3x_` entry points; their C bodies become one-line wrappers, and the `u3h_cin_*` exports
(which call those wrappers) stay, so every other caller is untouched.

| Function | C++ signature (`extern "C"`) | The one-line C wrapper passes |
|---|---|---|
| directory creation | `int u3x_cin_mkdirs(void *s, const char *path)` | `cin_mkdirs(s, path)`: both arguments unchanged (the wrapper can go: its only caller was the set-up) |
| stream defaults | `void u3x_cin_stream_init(uintptr_t base, uint8_t *st, const char *data_root)` | `cin_stream_init(base, st)`: its two arguments and the C part's compile-time data directory string `PLAT_DATA_DIR` (wrapper can go as well) |
| film set-up | `void u3x_cin_netinit(uintptr_t base, void *s, const char *data_root)` | `cin_netinit(base, s)`: its two arguments and `PLAT_DATA_DIR` |
| film scan | `void u3x_cin_scan(uintptr_t base, void *s, uint8_t *st)` | `cin_scan(base, s, st)`: unchanged |
| button flags | `void u3x_cin_flags(uint8_t *st)` | `cin_flags(st)`: unchanged |
| scan and free | `void u3x_cin_scan_free(uintptr_t base, void *s, uint8_t *st)` | `cin_scan_free(base, s, st)`: unchanged |
| date text | `void u3x_rfc2822(char *out, const int32_t *local, const int32_t *utc)` | `cin_rfc2822` stays as it is |
| select a film | `int u3x_film_select(uintptr_t base, void *s, int idx)` | `cin_select`: unchanged |
| protect / unprotect | `int u3x_film_protect(uintptr_t base, void *s, int on)` | `cin_protect`: unchanged |
| writer Open hook | `int u3x_film_open(uintptr_t base, void *s, const uint64_t *r)` | `cin_open_hook`: base, state, the register array |
| progress page hook | `int u3x_film_progress_page(uintptr_t base, void *s, const uint64_t *r)` | `cin_progress_page`: the same three |
| list toggle | `int u3x_film_toggle_list(uintptr_t base, void *s)` | `cin_toggle_list`: unchanged |
| Cinema entry | `void u3x_cinema(uintptr_t base, void *s)` | `cinema`: unchanged |

Parameters:

- `base`: game image base.
- `s`: the helper's state object (opaque; the fields used are in 3.2).
- `st`: a film stream object of the game (3.4). For the scan, the flags and scan-and-free it is the film state (the
  writer, image offset 0x17c3ba8); the callers always pass that address, but the functions must work on whatever
  `st` they are given.
- `data_root`: text, the helper's writable data directory without a trailing slash (`"/data"` in the PC build,
  `"/download0/u3mp"` in the PS4 build). It is a compile-time constant of the C part and the C++ part has no other
  way to learn it, which is why it is a parameter.
- `path`: text, an absolute directory path.
- `out`: buffer of at least 64 bytes. `local`, `utc`: two calendar records (3.8).
- `idx`: film index in the local list. `on`: non-zero = protect, 0 = unprotect.
- `r`: the saved registers (section 1).

Results: `u3x_cin_mkdirs` 0 (the directory exists now) or -1; `u3x_film_select` 0 or -1; `u3x_film_protect`,
`u3x_film_progress_page`, `u3x_film_toggle_list` always 1; `u3x_film_open` always 0.

Inside the group the new functions call each other directly (for example the select calls `u3x_cin_flags`); that is
the same as going through the `u3h_cin_*` exports. The one exception is the film set-up when it is started from
`u3x_film_open` or `u3x_cinema`: those two do not know `data_root`, so they call the C export
`u3h_cin_netinit(base, s)`, whose C side supplies it.

The old C++ part keeps several file-private helpers next to this code for functions that are not in this group
(`u3x_film_delete`, `u3x_film_record_header`, `u3x_film_prepare`, `u3x_film_delete_hook`). A new part must not
rely on them and must not reuse their names; keep the new private helpers in a namespace of their own.

Functions of the C part and of other groups that the new code calls (not described or reimplemented here):

| Name | Signature | What it does |
|---|---|---|
| `u3h_ext` | `uint8_t *u3h_ext(void)` | address of the helper's own state block ("ExtState", 3.1) |
| `u3h_cin_state` | `uint8_t *u3h_cin_state(uintptr_t base)` | the game's film state, = image offset 0x17c3ba8 |
| `u3h_cin_netinit` | `void u3h_cin_netinit(uintptr_t base, void *s)` | the C export of the film set-up: runs 4.4 with the build's data directory |
| `u3h_cin_path` | `char *u3h_cin_path(char *out, int size, const char *dir, const char *name)` | writes `dir`, a `/`, `name` into `out`, cut after `size` - 1 characters, NUL-terminated; returns `out` |
| `u3h_cin_cat` | `char *u3h_cin_cat(char *o, char *end, const char *t)` | copies the characters of `t` to `o` while there are characters and the write position is below `end`, writes a NUL at the final position, returns that position |
| `u3h_cin_num` | `char *u3h_cin_num(char *o, char *end, int64_t v, int width)` | decimal text of `v`: a `-` first when `v` < 0, then the digits of the magnitude padded with leading zeros to at least `width` digits (never cut to `width`, never more than 22 digits); each character only while the write position is below `end`; NUL at the final position, which is returned |
| `u3h_cin_localtime` | `const int32_t *u3h_cin_localtime(const int64_t *t)` | calendar record (3.8) of a time in seconds since 1970, in a static record of the C part. The port has no time zone: the fields are UTC. Never null. |
| `u3h_cin_game` | `int u3h_cin_game(uintptr_t base, void *s, int16_t *game, uint8_t *st)` | checks a film's net game index against the game's net game table; may replace `*game` by a valid one; 0 = no valid index, the net game must not be selected (other group) |
| `u3h_j07_rec` | `void u3h_j07_rec(uintptr_t base)` | notes the film writer's state in a diagnostic ring (other group) |
| `u3x_film_record_header` | `void u3x_film_record_header(uintptr_t base, uint8_t *st)` | writes the record-mode film header into the header area of `st` (existing C++ function, other group) |
| `u3x_film_entry_copy` | `void u3x_film_entry_copy(uint8_t *to, const uint8_t *from)` | copies one film list entry: bytes 0..0x200 and 0x208..0x1e8f; bytes 0x201..0x207 of the destination stay (existing, stays) |
| `u3x_sort_films` | `void u3x_sort_films(uint8_t *list, int count, uint8_t *spare)` | orders the first `count` entries newest first by the s64 at entry + 0x208; entries with equal times keep their order; moves entries with `u3x_film_entry_copy`; `spare` is one entry of scratch space (existing, stays) |
| `u3h_show` | `int u3h_show(uintptr_t base, void *s, uint32_t page, int silent)` | opens a menu page by id (other group) |
| `u3h_show_dialog` | `void u3h_show_dialog(uintptr_t base, void *s, uint32_t body, uint32_t title, uint32_t action, int32_t buttons, uint32_t extra, uint32_t close)` | fills the game's dialog record and opens the dialog page (other group) |
| `u3h_lan_party_size` | `int u3h_lan_party_size(uintptr_t base)` | number of members of the local party (other group) |

## 3. Game and helper facts

### 3.1 The helper's state block (ExtState, address from `u3h_ext()`)

| Offset | Type | Meaning |
|---|---|---|
| 0x3210 | pointer to function | kernel import 0: make directory |
| 0x3218 | pointer to function | kernel import 1: read directory entries |
| 0x3220 | pointer to function | kernel import 2: unlink (delete a file) |
| 0x3228 | pointer to function | kernel import 3: stat |
| 0x3230 | u32 | Cinema counter 0: 1 once the film set-up has run |
| 0x323c | u32 | Cinema counter 3: result of the last directory creation (0 or 0xffffffff) |
| 0x3240 | u32 | Cinema counter 4: films found by the last scan |
| 0x3244 | u32 | Cinema counter 5: files deleted by scans (bad films) |
| 0x3248 | u32 | Cinema counter 6: slots freed |
| 0x324c | u32 | Cinema counter 7: the descriptor (or error value) of the last scan's directory open |
| 0x3270 | 0x800 bytes | the directory read buffer |
| 0x3a70 | u32 | function switches; bit 1 (value 2) = Cinema port on |
| 0x4090 | u32 | counter: film set-ups run |
| 0x4094 | u32 | first stream word of the film selected last |
| 0x4098 | u32 | net game index of the film selected last (sign-extended s16) |
| 0x40a0 | u32 | counter: writer Open hook entries |
| 0x40a4 | u32 | counter: record headers written |
| 0x40a8 | u32 | "refresh done" flag of the progress page (0 / 1) |
| 0x40ac | u32 | the page the progress page moves on to (a page id; 0 = go back) |
| 0x40b4 | u32 | counter: list toggles |
| 0x40bc | u32 | counter: film bit streams given their buffer settings |
| 0x60000 | 20 × 0x1e90 bytes | storage of the local film list ("list A") |
| 0x86340 | 20 × 0x1e90 bytes | storage of the online file list ("list B"); = 0x60000 + 20 × 0x1e90 |
| 0xad000 | 0x20000 bytes | buffer of the writer's bit stream |
| 0xcd000 | 0x20000 bytes | buffer of the second bit stream |

The last four lie behind the ExtState record proper, in the helper's data segment; they exist only when the Cinema
port is built in, which is exactly when the switch bit is set.

"Cinema is on" = bit 1 of the u32 at 0x3a70 is set and all four pointers at 0x3210 .. 0x3228 are non-null.

### 3.2 The state object `s`

| Offset | Type | Meaning |
|---|---|---|
| 32 | pointer to function | file open |
| 40 | pointer to function | file read |
| 56 | pointer to function | file close |
| 728 | u32 | lobby error code shown by the helper's lobby |
| 784 | pointer to function | "native 2", the game's "select net game by index" (may be null). Natives are an array of pointers at 768; native k is at 768 + 8·k. |

### 3.3 Platform functions (System V x86-64 calling convention; all through the pointers above)

| Function | Arguments | Result | Notes |
|---|---|---|---|
| open (`s` + 32) | (path text, s32 flags, s32 mode) | 64-bit; **only its low 32 bits count, read as s32**: >= 0 a descriptor, < 0 an error | flags 0 = read only; 0x20000 = "must be a directory"; 0x601 = write only, create, truncate |
| read (`s` + 40) | (s32 descriptor, buffer, u64 byte count) | s64 bytes read | |
| close (`s` + 56) | (s32 descriptor) | ignored | |
| make directory (import 0) | (path text, s32 mode) | s32; 0 = created | mode 0x1c0 (owner read, write, search) |
| read directory entries (import 1) | (s32 descriptor, buffer, s32 buffer size, pointer to an s64 position word) | s32: number of bytes placed in the buffer; 0 = end; < 0 error | |
| unlink (import 2) | (path text) | s32; < 0 = failed | |
| stat (import 3) | (path text, pointer to a 0x78-byte record) | s32; 0 = the file exists and the record is filled | |

A directory entry in the read buffer: +4 u16 length of this entry in bytes (the next entry starts that many bytes
further), +6 u8 type (8 = regular file, 4 = directory), +8 the file name, NUL-terminated.

The stat record (0x78 bytes): +8 u16 file mode, +0x28 s64 modification time in seconds since 1970, +0x48 s64 file
size in bytes.

### 3.4 A film stream object (the writer = "film state" at image offset 0x17c3ba8, the reader at 0x17c5cf8)

| Offset | Type | Meaning |
|---|---|---|
| 0x08, 0x0c, 0x10, 0x14, 0x18 | u32 each | file state of the stream |
| 0xa0 | u32 | header version: 0x1c78000e (high half = header size 0x1c78, low half = film format number 14) |
| 0xb9 | text, 0x80 bytes | the film directory (absolute path, no trailing slash) |
| 0x140 | 0x1c78 bytes | the film header of the open / selected film |
| 0x1dc0 .. 0x1dc7 | u8 each | button flags of the Cinema page: 0x1dc0 and 0x1dc1 = Protect and Delete available, 0x1dc2 = Unprotect available, 0x1dc3 = Upload available; 0x1dc4 .. 0x1dc7 only ever cleared here |
| 0x1dc8 | text, 0x80 bytes | the directory relative to the data directory, with a trailing slash |
| 0x1e48 | s32 | selected local film |
| 0x1e4c | s32 | local film count |
| 0x1e50 | pointer | local film entries (list A) |
| 0x1e58 | s32 | online file count (this group never writes it) |
| 0x1e60 | pointer | online file entries (list B) |
| 0x1e8c | s32 | set to -1 by the set-up |
| 0x1fc9 | u8 | cleared by the set-up |

Only the writer has the fields from 0x1dc0 on; of the reader only 0x08 .. 0x18, 0xa0 and 0xb9 are written.

### 3.5 A film list entry (0x1e90 bytes; a list holds 20)

| Offset | Type | Meaning |
|---|---|---|
| 0x000 | text, 0x100 bytes | date text of the file's modification time |
| 0x100 | text, 0x100 bytes | file name |
| 0x200 | u8 | cleared by the scan |
| 0x208 | s64 | modification time, seconds since 1970 |
| 0x210 | u32 | file size (low 32 bits) |
| 0x214 | u32 | file mode. A film counts as unprotected exactly when (mode AND 0x180) = 0x180 |
| 0x218 | 0x1c78 bytes | the film header: +0 u32 header version, +4 s32 online flag of the recording (0 / 1) |

### 3.6 A film file, and the protection marker

A film file starts with the 0x1c78-byte header; the recorded bit stream follows. The stream is stored as u32 words;
read as little-endian u32, the first word is the stream format (14) and the second word carries the net game index
in its low 16 bits (signed).

This port cannot change file modes. A protected film is marked by an (empty) file of the same name in the
sub-directory `p` of the film directory: `<film directory>/p/<file name>`.

### 3.7 Game data and game functions

| Image offset | Type | Meaning |
|---|---|---|
| 0x16e7168 | u8 | list flag: non-zero = the local films are shown, 0 = the online files |
| 0x17bd080 | object | the menu (first argument of the menu functions) |
| 0x17c3838 | record | the writer's bit stream: +0 pointer to its buffer, +8 u32 bit position, +0xc u32 capacity in bits, +0x10 u32 |
| 0x17c3850 | record | the second bit stream, same layout |
| 0x17cb060 | object | the net game manager (first argument of native 2) |
| 0x17d5c68, 0x17d5c6c | s32 each | online play words: "online" = at least one of them is non-zero |
| 0x17d6010 | text | the player's online id (at most 16 characters) |
| 0x17d603c | u32 | net menu mode; the Cinema is mode 4 |
| 0x17d7b09 | u8 | debug switch: non-zero = films are kept whatever their online flag |
| 0x17dc298 | object | the party session: +8 pointer to its backend object (may be null), +0x18 u64 (0 = no session joined), +0x12e4 and +0x12e8 u32 each (equal = the local member is the session's owner) |

| Function | Arguments | Meaning |
|---|---|---|
| image offset 0x652200 | (menu = address of image offset 0x17bd080, s32 0); result unused | leaves the current menu page (back) |
| native 2 (`s` + 784) | (net game manager = address of image offset 0x17cb060, s32 index, null pointer, u32 0xc); result unused | selects a net game; index -1 = none |
| backend lock | the pointer at the backend object's +0 points to a table of function pointers; the one at table + 0x40 (may be null) takes (backend, party session, s32 1); result unused | locks the party session |

All use the System V x86-64 convention with integer and pointer arguments only.

### 3.8 The calendar record

Nine s32 words: 0 second, 1 minute, 2 hour (0..23), 3 day of month (1..31), 4 month (0..11), 5 year minus 1900,
6 day of week (0 = Sunday), 7 day of year (0 = 1 January), 8 unused here.

### 3.9 Ids and texts

- Pages: progress page 0x1d84d11e; film list page 0x735c4dbe.
- Dialog bodies: 0x06718e75 (Cinema not possible in a party), 0x5d425a5b (no online files), 0x2760b1fd (no films).
  Dialog title 0xc6467f2d, dialog close id 0xe1e040aa, action id of the party dialog 0x3a586923.
- Lobby error code: 5.
- Fixed texts: `"/cinema"`, `"cinema"`, `"/"`, `"p"`; the three-letter English day names starting with Sunday
  (Sun, Mon, Tue, Wed, Thu, Fri, Sat) and month names (Jan .. Dec) of e-mail dates (no game table); `", "`, `" "`,
  `":"`, `" +"`, `" -"`.

## 4. Behaviour

### 4.1 `u3x_rfc2822(out, local, utc)`: a date as e-mail headers write it

Pure: reads the two records, writes `out`, calls nothing.

The text is, in this order: the day name of `local` word 6 (taken modulo 7); `", "`; day of month as two digits; a
space; the month name of `local` word 4 (taken modulo 12); a space; year (`local` word 5 + 1900) as four digits; a
space; hour, `":"`, minute, `":"`, second, each as two digits; then `" +"` or `" -"`; then the zone's hours as two
digits and the zone's minutes as two digits. Example: `Thu, 12 Nov 2026 18:44:32 +0100`. All date fields come from
`local`; `utc` only enters the zone.

"As n digits" is the rule of `u3h_cin_num` with width n: a `-` first for a negative value, zero-padded to at least n
digits, longer numbers in full.

The zone, in minutes: D × 1440 + (local hour - utc hour) × 60 + (local minute - utc minute), where D is found from
D0 = (local year - utc year) × 366 + (local day of year - utc day of year): D = D0 when D0 is -1, 0 or 1; D = -1
when D0 is greater than 1; D = +1 when D0 is less than -1. The sign text is `" -"` when the zone is negative, else
`" +"`; hours = |zone| divided by 60 (rounded down), minutes = |zone| modulo 60.

Length limit: at most 63 characters are written; every character (also each single digit, sign or separator) is
written only while fewer than 63 have been written; a NUL follows the last character written. With fields in their
normal ranges the text has 31 characters.

Defined input range: day of week and month not negative (larger values are reduced modulo 7 / 12), and fields small
enough that none of the differences and products above leaves the s32 range. Nothing else is checked.

### 4.2 Directory creation `u3x_cin_mkdirs(s, path)`

Makes sure the directory `path` exists, creating missing parents first. Observable as the sequence of platform
calls:

1. Open `path` with flags 0x20000, mode 0. If that gives a descriptor (>= 0): close it; result 0; nothing else.
2. Otherwise determine the parent: the part of `path` before its last `/`, looking only at the first 0x7f characters
   of `path`. If there is such a `/` and it is not the first character, do the whole procedure for the parent first;
   if that fails (-1), the result is -1 and `path` itself is not created.
3. Call make-directory with the full, uncut `path` and mode 0x1c0. Result 0 when it returns 0, otherwise -1.

So for `/data/cinema14/Bob` with only `/data` present the calls are: open `/data/cinema14/Bob` (fails), open
`/data/cinema14` (fails), open `/data` (succeeds), close, make `/data/cinema14`, make `/data/cinema14/Bob`. A path
with no `/` beyond its first character is opened and, if that fails, created directly. Whether the open failed
because the directory is missing or for another reason is not distinguished.

### 4.3 Stream defaults `u3x_cin_stream_init(base, st, data_root)`

Writes, in the stream object `st`: 0 to the five u32 at 0x08, 0x0c, 0x10, 0x14 and 0x18; 0x1c78000e to the u32 at
0xa0; and the film directory text at 0xb9: `data_root`, then `/cinema`, then the film format number in decimal
(`14`), then `/`, then the player's online id (the text at image offset 0x17d6010). The text is cut after 0x7f
characters and NUL-terminated; bytes behind the NUL are left as they were. PC example for the id `Bob`:
`/data/cinema14/Bob`. An empty id gives a directory ending in `/`. No call is made.

### 4.4 Film set-up `u3x_cin_netinit(base, s, data_root)`

If Cinema is not on (3.1): nothing at all. Otherwise, with W = the film state (image offset 0x17c3ba8):

1. Both list storages (ExtState + 0x60000 and + 0x86340, 20 × 0x1e90 bytes each) are filled with zeros; the list A
   pointer of W (0x1e50) is set to the first, the list B pointer (0x1e60) to the second. The counts and the
   selection (0x1e4c, 0x1e58, 0x1e48) are **not** touched.
2. The text at W + 0x1dc8 becomes `cinema`, `14`, `/`, the online id, `/` (for example `cinema14/Bob/`), cut after
   0x7f characters, NUL-terminated.
3. The stream defaults (4.3) are applied to W.
4. The directory at W + 0xb9 is created with 4.2; its result (0, or -1 stored as 0xffffffff) goes to the counter at
   ExtState + 0x323c. A failure changes nothing else.
5. In W: the u8 at 0x1fc9 = 0, the s32 at 0x1e8c = -1, the eight bytes 0x1dc0 .. 0x1dc7 = 0.
6. The stream defaults (4.3) are applied to the reader (image offset 0x17c5cf8).
7. For the bit stream record at image offset 0x17c3838, then the one at 0x17c3850: if its buffer pointer is null it
   is set to the static buffer (ExtState + 0xad000 for the first, + 0xcd000 for the second); a non-null pointer is
   kept. Then its bit position (+8) = 0, its capacity (+0xc) = 0x100000 (0x20000 bytes in bits), its word at
   +0x10 = 0, and the counter at ExtState + 0x40bc grows by 1 (so by 2 per set-up).
8. The counter at ExtState + 0x3230 = 1; the counter at ExtState + 0x4090 grows by 1.

The only platform calls are those of step 4. Calling the set-up again repeats everything (lists emptied again).

### 4.5 Film scan `u3x_cin_scan(base, s, st)`

Rebuilds list A from the film directory `st` + 0xb9.

If the list A pointer of `st` is null: the local film count becomes 0 and nothing else happens (no call, no
counter).

Otherwise:

1. Open the directory (flags 0x20000, mode 0). The value (low 32 bits) is stored in the counter at
   ExtState + 0x324c, also when it is an error.
2. If the open succeeded, read the directory in blocks: call read-directory-entries with the descriptor, the
   0x800-byte buffer at ExtState + 0x3270, the size 0x800 and the address of an s64 that holds 0 before each call.
   A result of 0 or less ends the reading. Otherwise walk the entries of the block from its start; the walk of a
   block ends when the position reaches the number of bytes returned, or at an entry whose length is below 8 (the
   next block is then read). Reading and walking stop for good as soon as 20 films are kept; the rest of the
   directory is not looked at.
3. For each entry of type 8 (other types are skipped without any call), with N = the number of films kept so far and
   E = slot N of list A (list + N × 0x1e90); E is written **in place, before the film is known to be good**, so a
   candidate that is dropped later leaves its data in slot N:
   1. Build the file path with `u3h_cin_path` (0x180-byte buffer): directory, `/`, name.
   2. stat it into a zero-filled 0x78-byte record. Non-zero result: skip the entry; E is not touched.
   3. Date text: the first byte of E becomes NUL, then `u3x_rfc2822(E, C, C)` where C is the calendar record of the
      modification time from `u3h_cin_localtime` (the port has no time zone; local and UTC fields are the same, the
      zone is always `+0000`). The s64 at E + 0x208 = the modification time.
   4. The file name is copied to E + 0x100, cut after 0xff characters, NUL-terminated.
   5. The u32 at E + 0x210 = the low 32 bits of the file size; the u8 at E + 0x200 = 0; the u32 at E + 0x214 = the
      u16 file mode.
   6. Marker test: stat `<directory>/p/<name>` (built with two `u3h_cin_path` calls, 0x180-byte buffers; a fresh
      zero-filled record). If the result is 0 the u32 at E + 0x214 becomes 0x100. This stat is made for every entry
      that passed step 2.
   7. Open the file (flags 0, mode 0). Failure: skip the entry (the file stays on disk, it is not counted).
   8. Read 0x1c78 bytes into E + 0x218, then close the file.
   9. The film is **kept** (N grows by 1) when all of these hold: exactly 0x1c78 bytes were read; the u32 at
      E + 0x218 equals the header version of `st` (the u32 at `st` + 0xa0); and either the debug switch (image offset
      0x17d7b09) is non-zero or the s32 at E + 0x21c equals the current online flag (1 when "online" as in 3.7,
      else 0).
   10. Otherwise the file is **deleted**: unlink with the file path (result ignored), and the counter at
       ExtState + 0x3244 grows by 1.
4. Close the directory if it was opened.
5. `u3x_sort_films(list A, N, spare)` with a 0x1e90-byte static scratch entry of the new part: newest first.
6. The local film count (`st` + 0x1e4c) = N; the counter at ExtState + 0x3240 = N.

Bytes of a slot that the steps above do not write (the remainder of the two text fields behind their NUL, the bytes
0x201 .. 0x207, a header only partly read) keep their previous contents. The selection is not changed. Order of the
platform calls for one regular file: stat, stat of the marker, open, read, close, and possibly unlink.

### 4.6 Button flags `u3x_cin_flags(st)`

Pure on memory. The eight bytes `st` + 0x1dc0 .. 0x1dc7 are set to 0. If the list A pointer is null, that is all.
Otherwise let E be the entry of list A at the selected index (the s32 at 0x1e48 taken as an unsigned 32-bit number;
**no** comparison with the film count, so an entry beyond the count is read as it is):

- E unprotected ((mode AND 0x180) = 0x180): the bytes at 0x1dc0 and 0x1dc1 become 1. Otherwise the byte at 0x1dc2
  becomes 1.
- The byte at 0x1dc3 becomes 1 unless list B is present (pointer non-null) and one of its first "online file count"
  entries has a file name (entry + 0x100) equal, byte for byte up to and including the NUL, to E's file name. With
  an online file count of 0 the byte is always set. (The count is expected to be 0 or more.)

### 4.7 Scan and free a slot `u3x_cin_scan_free(base, s, st)`

Runs the scan (4.5). If the film count is then not exactly 20, nothing more. Otherwise the entries are examined
from index 19 down to 0 (oldest first, since the list is sorted newest first); protected entries are passed over.
For an unprotected one the file path is built (`u3h_cin_path`, directory of `st`, entry name, 0x180 bytes) and
unlink is called: a negative result means "try the next lower index"; otherwise every entry behind it moves one
place towards the front (`u3x_film_entry_copy`, ascending, so the last slot keeps a copy of its old contents), the
film count becomes 19, the counter at ExtState + 0x3248 grows by 1, and the function ends. If no film could be
deleted the count stays 20.

### 4.8 `u3x_film_select(base, s, idx)`

Works on the film state W = `u3h_cin_state(base)`.

1. Result -1 without any effect when `idx` is not below the local film count, or when the list A pointer is null.
   (`idx` is not compared with 0; callers pass 0 or a list row.)
2. The selected film (W + 0x1e48) = `idx`.
3. Build the path of entry `idx` (directory of W, entry name; 0x180 bytes) and open it (flags 0, mode 0). Failure:
   result -1; the selection stays changed, nothing else happens (the button flags are not refreshed).
4. Read 0x1c78 bytes into the header area W + 0x140; then read 0x100 bytes into a scratch block that was filled with
   zeros before; close. The read results are not looked at (a short file leaves zeros in the scratch block).
5. From the scratch block: the first u32 goes to ExtState + 0x4094; the net game index G = the low 16 bits of the
   second u32 as s16; G sign-extended goes to ExtState + 0x4098.
6. Call `u3h_cin_game(base, s, &G, W)` (always). If it returns non-zero and native 2 is non-null, call native 2 with
   (net game manager, G as it is after that call, sign-extended to s32, null, 0xc).
7. Refresh the button flags (4.6) on W. Result 0.

"Cinema is on" is not tested here.

### 4.9 `u3x_film_protect(base, s, on)`

Result always 1. Nothing happens when Cinema is not on, or when the list A pointer of the film state W is null.
Otherwise, with E = the selected entry (selection taken as unsigned 32-bit, no range check, as in 4.6):

1. Make-directory `<film directory>/p` with mode 0x1c0; the result is ignored (it normally exists already).
2. Marker path M = `<film directory>/p/<E's file name>` (two `u3h_cin_path` steps, 0x180-byte buffers).
3. `on` non-zero: open M with flags 0x601 and mode 0x180. If that gives a descriptor it is closed and the step
   counts as done. `on` zero: unlink M (result ignored), then stat M (zero-filled 0x78-byte record): the step counts
   as done when stat reports a non-zero result (the marker is gone).
4. Only when the step is done: the u32 at E + 0x214 becomes 0x100 (`on` non-zero) or 0x180 (`on` zero).
5. Refresh the button flags (4.6) on W, in every case.

### 4.10 `u3x_film_open(base, s, r)`: before the game opens a film stream

Result always 0 (the game's Open runs afterwards). In this order:

1. The counter at ExtState + 0x40a0 grows by 1.
2. Stop when Cinema is not on, when `r[4]` (the stream being opened) is not the address of the film state, or when
   the low 8 bits of `r[5]` (the open mode) are not 0. Mode 0 is "record".
3. If the counter at ExtState + 0x3230 is 0, run the film set-up through `u3h_cin_netinit(base, s)`.
4. `u3h_j07_rec(base)`.
5. Scan and free a slot (4.7) on the film state.
6. If the film count is not 20: `u3x_film_record_header(base, film state)`, and the counter at ExtState + 0x40a4
   grows by 1. (With 20 films left the game's Open refuses the recording by itself.)

### 4.11 The list refresh and the progress page (shared by 4.12, 4.13, 4.14)

On PS3 the film list is refreshed by a worker thread while a progress page is shown; the page handler waits for the
worker's "done" flag and then moves on. Here the refresh runs at once on the calling thread:

**Start a refresh (target page T, parameter P).** In this order: the done flag (ExtState + 0x40a8) = 0; the target
(ExtState + 0x40ac) = T; `u3h_show(base, s, 0x1d84d11e, 0)`; then the refresh itself: done flag = 0; if P is 0, the
film scan (4.5) on the film state (a non-zero P stands for the online file list, which this port never fetches);
done flag = 1.

### 4.12 `u3x_film_progress_page(base, s, r)`

Result always 1. `r[4]` is a pointer to the page event, an array of s32; word 0 is the event kind. Nothing happens
when that pointer is null or Cinema is not on. Otherwise:

- Event kind 6: nothing while the done flag (ExtState + 0x40a8) is 0. When it is non-zero:
  1. If the target (ExtState + 0x40ac) is 0: call game function 0x652200 with (menu, 0). Otherwise
     `u3h_show(base, s, target, 0)`.
  2. Then, if the list flag (image offset 0x16e7168) is 0 (online list): when the online file count (film state
     + 0x1e58) is 0, `u3h_show_dialog(base, s, 0x5d425a5b, 0xc6467f2d, 0, 0, 0, 0xe1e040aa)` and afterwards the list
     flag = 1. If the list flag is non-zero (local list): when the local film count is 0,
     `u3h_show_dialog(base, s, 0x2760b1fd, 0xc6467f2d, 0, 0, 0, 0xe1e040aa)`.
  The done flag and the target are left as they are.
- Event kinds 2, 3 and 4: refresh the button flags (4.6) on the film state.
- Any other kind: nothing.

### 4.13 `u3x_film_toggle_list(base, s)`

Result always 1. Nothing when Cinema is not on. Otherwise: the list flag (u8 at image offset 0x16e7168) has its
lowest bit inverted (the other bits stay); the counter at ExtState + 0x40b4 grows by 1; then a refresh is started
(4.11) with target 0 and parameter = the list flag's value **before** the change. So switching from the online to
the local list scans the films; switching to the online list scans nothing.

### 4.14 `u3x_cinema(base, s)`: the lobby's Cinema entry

In this order:

1. If `u3h_lan_party_size(base)` is greater than 1:
   `u3h_show_dialog(base, s, 0x06718e75, 0xc6467f2d, 0x3a586923, 0, 0, 0xe1e040aa)`; stop.
2. If native 2 is null: the lobby error (u32 at `s` + 728) = 5; stop.
3. The net menu mode (image offset 0x17d603c) = 4.
4. Party session lock: when the backend pointer of the party session is non-null and (the u64 at session + 0x18 is 0
   or the u32 at + 0x12e4 equals the u32 at + 0x12e8), take the backend's lock function (3.7); when it is non-null
   call it with (backend, party session, 1).
5. When Cinema is on: if the counter at ExtState + 0x3230 is 0, run the film set-up through
   `u3h_cin_netinit(base, s)`; then the film scan (4.5) on the film state.
6. Call native 2 with (net game manager, -1, null, 0xc).
7. When Cinema is on: `u3x_film_select(base, s, 0)`, result ignored.
8. Start a refresh (4.11) with target 0x735c4dbe and parameter 1 (no second scan). This step also runs when Cinema
   is off.

### 4.15 Threads

The group starts no thread, waits for nothing and takes no lock. Every function runs to its end on the game thread
that called it: the hooks on the thread that runs the hooked game function (menu handlers; the writer's Open), the
set-up also on the thread of the helper's NetInit step. Nothing in the old code protects the shared data, which is:
the directory read buffer, the scratch entry of the sort, the two lists and the film state, the calendar record of
the C part, the done flag and the target. The functions are not re-entrant and must not be made to yield (no sleep,
no deferred work): the callers rely on the list and the done flag being final when the call returns.

## 5. Test scenarios

Three kinds of test are possible; the list says which part belongs where.

**A. Plain buffers (pure, old against new, also with random inputs).**

- `u3x_rfc2822`: 1 January 1970 00:00:00 with equal records (`Thu, 01 Jan 1970 00:00:00 +0000`); a normal date with
  local one hour ahead (`+0100`); local behind (`-0500`); half-hour zones (+330 minutes: `+0530`; -210: `-0330`);
  local on the next day of the same year (D0 = 1) and on the previous day (D0 = -1); the year change with the
  earlier year a leap year (utc 31 December = day 365, local 1 January: D0 = 1, `+0100` for one hour) and a common
  year (day 364: D0 = 2, so D = -1 and, for local 00:30 against utc 23:30, the text ends in ` -4700`), and both the
  other way round; every day of week and month; day of week 7..13 and month 12..23 (reduced); single-digit and
  two-digit fields; a year below 1000 and above 9999; a negative day of month; records that make the text longer
  than 63 characters (cut exactly at 63, NUL at `out` + 63); bytes of `out` behind the NUL untouched.
- `u3x_cin_flags` on a fake film state with fake lists: list A null (only the eight zero bytes); selected film
  unprotected with mode 0x180, 0x1ff, 0x81b6; protected with 0x100, 0x080, 0; list B null; online count 0; name
  present in list B at the first / last counted position; present only beyond the count; a name that is a prefix of
  a list B name and the reverse; selection beyond the film count (the entry there is used); the bytes 0x1dc4 ..
  0x1dc7 preset to non-zero (cleared).
- `u3x_cin_stream_init` on a fake stream and a fake image: every field; ids of 0, 1 and 16 characters; a
  `data_root` long enough to hit the 0x7f cut; bytes behind the NUL unchanged.

**B. A fake OS layer** (recording stand-ins for open / read / close in a fake state object and for the four kernel
imports in a fake ExtState, over an in-memory directory tree; recording stubs for the `u3h_` helpers and the game
calls; compare memory, results and the sequence and arguments of all calls, old against new):

- Directory creation: path exists; only the last part missing; two parts missing; nothing exists up to the root;
  parent creation failing (path itself not attempted, -1); make-directory of the last part failing; a path without
  an inner `/`; a path longer than 0x7f characters; an open result whose low 32 bits are negative while the 64-bit
  value is positive (must count as failure).
- Film set-up: Cinema off in each of the five ways (nothing written); normal run (every field of 4.4, both lists
  zeroed from non-zero contents, counts and selection untouched, counters); bit-stream buffer pointers null and
  preset (kept); directory creation failing (counter 0xffffffff, the rest still done); a second run.
- Scan: list A null; directory open failing (count 0, counter 7 = the error value, no close, counters 4);
  empty directory; one good film; entries of type 4 and other types; an entry length below 8 in the middle of a
  block (the next block is read); a block boundary; read-directory-entries returning a negative value; stat
  failing; marker present and absent; open failing (slot written, not counted, not deleted); short read; wrong
  header version; online flag 0 / 1 against each of the two online words, with the debug switch 0 and non-zero;
  more than 20 good films (stops at 20, the others neither read nor deleted, also across a block boundary);
  several films with different and with equal times (order); a dropped candidate followed by a kept one in the same
  slot, and a dropped candidate as the last one (its data stays in slot N); a long file name (0xff cut, path cut);
  stale text behind the NULs preserved; sizes above 4 GiB (low 32 bits).
- Scan and free: 19 films; 20 with the oldest unprotected (count 19, no entry moved); 20 with only a middle one
  unprotected (the entries behind it move up, slot 19 keeps its old contents); unlink failing for the oldest
  unprotected (the unprotected film at the next lower index is taken); all protected, or every unlink failing (count stays 20).
- Select: `idx` equal to and above the count; list null; open failing (selection changed, -1, flags untouched);
  normal film (header area, the two diagnostic words, the `u3h_cin_game` call and its arguments, native 2 with the
  possibly replaced index, flags refreshed); `u3h_cin_game` returning 0 (no native call, flags still refreshed);
  native 2 null (still one `u3h_cin_game` call); a net game word with the high half set and a low half of 0xffff
  (-1); a file shorter than the header, and one that ends inside the 0x100 bytes.
- Protect / unprotect: Cinema off; list null; protect with the marker creatable and not creatable; unprotect with
  the marker present, absent, and present but not removable (mode word unchanged); the flags refresh in every case;
  the ignored make-directory result (0 and non-zero); the exact open flags and modes.
- Writer Open hook: Cinema off; another stream in `r[4]`; mode byte non-zero (also with higher bytes of `r[5]` set
  and the low byte 0, which is record mode); set-up needed and not needed; 19 films (header written, counter); 20
  films all protected (no header); 20 films with one freed (header written).
- Progress page: event pointer null; Cinema off; event 6 with the done flag 0; with target 0 and non-zero; local
  list with 0 and with some films; online list with 0 files (dialog, flag back to 1) and with files; events 2, 3,
  4 (flags); events 0, 1, 5, 7.
- Toggle: Cinema off; flag 1 to 0 (no scan); flag 0 to 1 (scan); a flag value of 2 (becomes 3, no scan); order: flag,
  counter, done = 0, target, show, scan, done = 1.
- Cinema entry: party of 2 (dialog only); native 2 null (error 5 only); backend null; backend present with the u64
  zero, with the two u32 equal, with neither (no lock); lock function null; Cinema off (mode 4, lock, native 2 with
  -1, progress page, done = 1, no scan, no select); Cinema on with set-up needed / done, with 0 films (select
  returns -1) and with films; the order of all calls.

**C. The live game only.**

- That the real platform functions deliver what 3.3 says (entry and stat layouts, error values, flags) in the PC
  runtime and on the PS4.
- That the pages and dialogs really appear and the buttons follow the flag bytes; that a film recorded in a match
  shows up, can be selected and started; that Protect survives a restart; that the 21st recording replaces the
  oldest unprotected film; that a NetInit after a profile change moves the directory to the new id.
- On which thread the game calls its film writer's Open (see open question 1).

## 6. Oddities of the old code, to be kept

1. Date text, year change: the day difference uses 366 for a year step, so across the end of a common (365-day)
   year the zone gets the wrong sign and size (4.1 and the ` -4700` scenario). It cannot occur in this port (local
   time = UTC) but is part of the function.
2. Date text: a negative day of week or month is not guarded; every part is written with its own 63-character test.
3. Directory creation does not look at why the open failed; a parent path is only derived from the first 0x7f
   characters.
4. Film set-up: no guard of its own against a second run; it empties both lists but leaves the film count, the
   online count and the selection, so until the next scan the count can name entries that are all zero. The test
   "buffer present" before the bit-stream settings can never fail. The directory creation result is only recorded.
5. Scan: any regular file in the film directory that is not a complete film of the current header version, or that
   was recorded with the other online flag (LAN film while "online", and the reverse), is **deleted**, unless the
   debug switch is set. Files that cannot be stat-ed or opened are skipped, not deleted. Marker files without a
   film are never removed. The directory position word is reset to 0 before every block read.
6. Scan: a slot is filled before the film is validated; dropped candidates leave their data behind the last kept
   film. Slots are never cleared between scans.
7. Scan: the mode word is the raw stat mode (16 bits) or 0x100; Unprotect later writes exactly 0x180.
8. Scan with a null list writes only the count (the "films found" and "last open" counters keep their old values).
9. Button flags, select, protect: the selection is used as an unsigned index without comparing it with the count
   (and select accepts a negative `idx`, which then addresses memory far outside the list). Select changes the
   selection before it knows that the file opens, and returns -1 then without refreshing the flags.
10. Button flags: the name search does not stop at the first match and treats a negative online count as "very
    many"; with this port the online count is always 0, so Upload is always offered.
11. Select: both reads are unchecked; with a short file the net game index is 0.
12. Protect: the `p` directory is created on every press; unprotect judges success by the marker being absent
    afterwards, not by the unlink result; with an empty selected name the marker path is the `p` directory itself.
13. Scan and free: an unlink result of 0 or more counts as deleted; the vacated last slot is not cleared.
14. Progress page: the done flag is never cleared by the handler, so every further event 6 repeats the page change
    and the dialog test. The online dialog sets the list back to local after it was shown.
15. Toggle: only bit 0 of the list flag is inverted; the refresh parameter is the old flag value as a number.
16. Cinema entry: when Cinema is off the mode is still set to 4, the party is locked, the net game deselected and
    the progress page shown (it then never moves on, because the page handler does nothing while Cinema is off).
    "Cinema is on" is evaluated again before the select.
17. The refresh sets the done flag to 0 twice (before the page is shown and again when the refresh starts).

## 7. Open questions

1. Threads: the old code has no locking at all. If the game calls its film writer's Open on another thread than the
   menu's, a scan started from the menu and the scan of the Open hook could overlap on the shared read buffer and
   list. The old code accepts that; the new code must not add locking (it would change timing), but the tester
   cannot see this on a fake OS layer.
2. The date text in the scan: the old C path fetched the UTC record through a second C-part function that fills a
   second static record with the same values. The C part exports only `u3h_cin_localtime`; passing its record for
   both arguments gives the same text, and the only difference is that the C part's second static record is no
   longer written (nothing else reads it). If the integrator prefers, the C function `cin_rfc2822(char *, int64_t)`
   can be exported and called instead.
3. `data_root` as a parameter is this document's proposal for the compile-time directory string; the alternative is
   a new C export returning it.

## 8. Clarifications (tester, 2026-11-14)

Facts settled by the old-against-new test (`reimpl2/G05/test_g05.cpp`), in words. They add to the sections above;
where one of them is narrower or wider than the text above, this section counts.

1. **Date text (4.1): which steps are 32-bit.** Only these steps are made in signed 32 bits: the four field
   differences local minus utc (year, day of year, hour, minute), the hour difference × 60, and year + 1900. The day
   difference D0 = year difference × 366 + day-of-year difference and the zone total D × 1440 + (hour difference
   × 60) + minute difference are formed without loss (64 bits), and the zone's hours and minutes are taken from that
   64-bit value. The defined input range is therefore "those six 32-bit steps do not overflow", nothing more; the
   year product and the sum may be larger than 32 bits. Two examples (all fields not named are equal in both
   records): utc year word 842339022 against local 106 gives D0 far below -1, so D = +1 and the text ends in
   ` +2400`; local 13:27 against utc hour 32375059, minute 680811528 gives a zone of -2623314261 minutes and the
   text ends in ` -4372190421`. (In this port both records are always the same, so the zone is always `+0000`.)
2. **Date text: the end.** With equal records and all five numbers in range the text has 31 characters. A text of
   exactly 63 characters is written in full with the NUL at `out` + 63. Whatever the fields hold, at most 57
   characters stand before the seconds, so the limit can only fall inside the digits of the seconds or inside the
   zone part (its sign text, hours, minutes); the day and month names, the separators before the seconds and the
   `-` of a negative number are always written.
3. **Scan (4.5): an entry with a length below 8** is not examined at all: no path is built and no call is made for
   it, whatever its type, also when it is the first entry of the block. The entries behind it in the same block are
   not looked at; the next block is read. An entry that starts below the returned byte count is examined even when
   its length reaches beyond it.
4. **Platform results.** Of the results of stat, unlink, make-directory and read-directory-entries only the low 32
   bits count; whatever the upper half holds is ignored. stat: every non-zero value (also a positive one) means
   "not there". make-directory in the directory creation: only 0 is success, a positive value is a failure.
   unlink in scan-and-free: a negative value is a failure, 0 and every positive value mean "deleted". An open
   result with a non-negative low half is a descriptor also when the 64-bit value is negative, and the descriptor
   handed on to read / close / read-directory-entries is that low half.
5. **Online id (4.3, 4.4): not capped at 16 characters.** The whole text at image offset 0x17d6010 up to its NUL is
   used; only the 0x7f cut of the directory texts limits it.
6. **Indexes are unsigned (4.6, 4.8, 4.9).** `u3x_film_select` compares `idx` with the count as signed numbers, so a
   negative `idx` passes; it is stored as the selection as it is, and the entry used is list + (`idx` as unsigned
   32-bit) × 0x1e90, i.e. for -1 the entry with index 0xffffffff, not the entry before the list. The button flags
   and protect take the selection the same way.
7. **Button flags (4.6): the name search** starts at entry 0 of list B and goes on until the entry index equals the
   online file count; it does not stop at a match. With a count of 0 nothing is compared. A negative count, or one
   beyond the storage, makes the search run on through the memory behind list B (it ends in a fault), also when a
   name already matched. For counts from 0 to the size of the storage "unsigned, below the count" and "until equal"
   are the same thing.
8. **Protect (4.9): order.** The path of the `p` directory is built; make-directory is called with it (mode
   0x1c0); then the marker path is built; then open (flags 0x601, mode 0x180) and close, or unlink and stat (zero
   filled record). These platform calls and their arguments are all that can be observed. The old code builds
   paths more often than needed (the `p` directory path a second time for the marker, and for unprotect the whole
   marker path a third time before the stat: 3 path calls for protect, 5 for unprotect); `u3h_cin_path` is a pure
   text builder, so building each path once (2 calls) is the same behaviour.
9. **Cinema entry (4.14): native 2 is read once**, in step 2. That value is the function called in step 6, also
   when the table entry at `s` + 784 was changed in between (by the lock function or during the scan).
   `u3x_film_select` in step 7 reads the table entry again by itself. "Cinema is on" is evaluated twice (steps 5
   and 7), each time from the current memory.
10. **Progress page (4.12).** "Done" is any non-zero value of the done flag, not only 1. The list flag, the online
    file count and the local film count are read after the page change of step 1 has returned. The online list is
    exactly "list flag = 0"; "no online files" is exactly "count = 0" (a negative count shows no dialog).
11. **Refresh (4.11).** The second "done flag = 0" is written after `u3h_show` has returned and before the scan
    starts, so a value the page call left in the flag is overwritten before the first platform call of the scan.
12. **Scan-and-free (4.7): the count after a deletion.** The old code reads the film count again after the
    successful unlink, moves the entries up to that count and stores that count minus 1. The count was found to be
    20 just before and nothing in between can change it, so "19 entries remain, the count becomes 19" is the same in
    every state that can occur.
13. **Storage (3.1).** List B ends at ExtState + 0xac680; the 0x980 bytes up to the first bit stream buffer at
    0xad000 belong to nothing.
14. **Stand-ins that are free.** The old C functions reached the path builder, the two text helpers and the
    calendar record as C statics; the old C++ functions reached the button flags, the scan and scan-and-free through
    their `u3h_cin_*` exports. Calling the group's own functions directly, building texts with own helpers instead
    of `u3h_cin_cat` / `u3h_cin_num`, taking the film state address from `base` instead of `u3h_cin_state`, and one
    `u3h_cin_localtime` call per file (open question 2: the C part's second static record is then no longer
    written) change nothing that the test can see.
