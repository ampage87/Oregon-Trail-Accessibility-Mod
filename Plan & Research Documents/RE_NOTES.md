# Oregon Trail Deluxe 3.0 (DOS): reverse-engineering notes and mod design

## The game binary

- `OREGON.EXE` is 273,648 bytes (SHA-1 6fcbc2814ffccc295a5b144d3f83c310a82ff6b7).
- It was built with Borland C++ (1991), large memory model, using the Genus GX graphics kernel and VROOMM overlays. Resources live in `OREGON.GXL`.
- Main-image addresses below are written `mSSSS:OOOO` (relative segment) and overlay addresses `oN:OOOO`.
- File offset of main-image code = 0x1C00 + seg×16 + off.
- The Borland segment table is at file 0x1B720. Overlay far calls go through that table's byte index, not through real segments.
- The overlay block (FBOV) starts at file 0x24F70. Overlay 12 begins at file 0x2DD70.
- DGROUP is relative segment 0x1AB5. Its runtime segment is load segment + 0x1AB5.
- The game's text uses the Macintosh character set: 0x8E is é, and 0xC9 draws the division sign on the score screen.

### GXL archive

- The entry table starts at 0x80.
- Each entry is 26 bytes: flag, name[13], offset (u32), size (u32), date and time.

### Screens: CTR scripts

Every screen and dialog is a CTR script in the GXL archive.

**Script opcodes:**

| Opcode | Meaning |
|---|---|
| 1 | Images |
| 4 | Height |
| 5 | Width |
| 6 | X |
| 7 | Y |
| 8 | Button (image, frame, id, hotkey scan code) |
| 9 | Radio (image, var, value, …) |
| 10 | Text |
| 11 | Field |
| 12 | Fill |
| 14 | Scroll list |
| 17 | Comment |
| 18 | Font |
| 19, 20 | Relative moves |

**Dialog functions:**

| Function | Address |
|---|---|
| `LoadCTR(name)` | o12:0000. Its epilogue at o12:0E17 returns the dialog in DX:AX from `[bp-2E]:[bp-30]` |
| `FreeDialog` | o12:0E4D |
| `DrawDialog` | o12:0F72 |
| `HandleEvent` | o12:1055 |
| `DoDialog` | o12:1527 (rarely used) |

**Dialog struct:** +0 control count, +2 far pointer to an array of far pointers to controls.

**Control words:**

- 0 h, 1 w, 2 x, 3 y, 4 frame (a selected radio has 1), 5 image index, 6 type.
- Types: 8 button, 9 radio, 10 text, 11 field, 12 fill, 14 list.
- Button: id is w7, hotkey scan code is w9 (28 Enter, 1 Esc, 59 F1).
- Radio: value is w7, variable is w8.
- Field: id w7, maximum length w11, cursor at byte 0x18, focus bit at byte 0x1E, buffer far pointer at byte 0x24.
- Scroll list: w11 item count, w12 selected item, w13 top visible item, and w18:w19 a far pointer to an array of far pointers to the entry strings. The mod reads the whole list from there. To select an entry it sets w13 so the entry is on screen, then clicks its row. Rows are 12 px apart. The arrows sit in the scroll bar just right of the list (x + w + 7), at the top and bottom.

**Other behaviour:**

- Screens are often loaded ahead of time. For example, the save box also loads `OVERWRTE`, `READONLY` and `BADFNAME`. A screen only appears when `DrawDialog` runs on it.
- The game takes no keyboard input for lists. Lists are scrolled by clicking the arrows.

### Text

- `txPutString` is m17B8:0050. Its worker is m17B8:00B1, Pascal convention: `[bp+6]` y, `[bp+8]` x, `[bp+0A]` string.
- The word-wrap routine m18EC:0000 calls the worker with coordinates relative to its box.

### Input

- The event queue code is in m107C:
  - `evGetEvent` m107C:0164 (returns 0 on success)
  - `evPostEvent` m107C:01C0
  - keyboard poll in INT 1Ch at m107C:02FB
- Event record (14 bytes): code (mouse bits 1 move, 2 left press, 4 left release; keyboard 0x8000), ascii, scan, shift, buttons, x, y, time.
- The game keeps its own cursor at DS:5C68 and DS:5C6A (`msGetPos`, m0EEC:09F2).
- INT 66h belongs to the game's DIGPAK and MIDPAK sound drivers. The game calls it 33 times.

### Game state (struct passed to o15:00E0, 0xA5 bytes)

| Offset | Field |
|---|---|
| 0 | Names, 5 × 15 bytes |
| 0x4B | Occupation |
| 0x4D.. | Conditions per person (0–8 illness or injury, 9 none, 10 dead) |
| 0x63 | Health (÷35: good, fair, poor, very poor, dying) |
| 0x69 | Tongues |
| 0x6B | Wheels |
| 0x6D | Axles |
| 0x6F | Bullets |
| 0x71 | Clothes |
| 0x73 | Food |
| 0x75 | Oxen |
| 0x79 | Money, in cents (dword) |
| 0x7D | Pace |
| 0x7F | Rations |
| 0x81 | Year |
| 0x83 | Month (0-based) |
| 0x85 | Day |
| 0x87 | Miles (dword) |
| 0x8B | Next landmark |
| 0x8D | Route (bit 0 Green River shortcut, bit 1 Dalles shortcut) |
| 0x8F | Stopped |
| 0x91 | Weather |
| 0x93 | Temperature |
| 0x9D | Resting |
| 0x9F | Delayed |

- The mileage table (4 routes × 19 landmarks) is at DS:0C58.
- The date turns before the day's miles are added. Daily reports therefore wait for the miles to change, or 1.2 seconds on days without travel.
- Hunting time is at DS:7798, in seconds. It is also saved in `OT.CNF` at byte 42.

### Hunting (overlay 6, sprite table passed to o6:1949)

- The table holds 19 far pointers to sprites.
- Sprite fields: +4 x, +6 y, +8 h, +0A w, +10 interval (ms), +14 last move, +18 speed, +1A type, +1C direction (1 right, 2 left), +1E kind, +2A/+2C bullet target.
- Types: 1 dead, 2 alive, 3 hit, 6 bullet.
- Kinds: 0 buffalo, 1 deer, 2 rabbit, 3 squirrel, 4 elk, 5 bear, 6 pronghorn.
- The gun is at (317, 339). The bullet flies to the clicked point, and the hit test is pixel-accurate when it arrives.

### Rafting (overlay 10, object table passed to o10:057F)

- Object fields: +20 x, +22 y, +24 height, +26 width, +30 active, +18 move interval (75 ms for the raft).
- Object 0 is the raft (72 px wide). Objects 1–3 are rocks. Object 4 is the map marker.
- Each interval, the raft moves 8 px right while the cursor is right of its right edge, and 8 px left while the cursor is left of its left edge. Raft x is limited to 0x50 .. 0x1CC − width.

## How the mod hooks the game

### `OREGONM.EXE`

`OREGONM.EXE` is a copy of `OREGON.EXE` with nine hook sites replaced by `CD 61 nn` (INT 61h, hook number). The list is in `src/companion/patcher.cpp` and `tools/patch_exe.py`.

| # | Site | Purpose |
|---|---|---|
| 1 | m17B8:00B1 | Text drawn (x, y, caller, string) |
| 2 | o12:0E17 | Screen script loaded (dialog pointer, name) |
| 3 | o12:1527 | DoDialog |
| 4 | o12:0E4D | Dialog freed |
| 5 | m107C:0164 | Event poll (heartbeat, travel hold) |
| 6 | o15:00E0 | Game state pointer |
| 7 | o6:1949 | Hunting sprite table |
| 8 | o10:057F | Rafting object table |
| 9 | o12:0F72 | Dialog drawn (repeat draws are not recorded) |

### `OTMOD.COM` (`src/dos/otmod.asm`)

This is a TSR loaded before the game.

**Mailbox** (signature `OTACCESS-MBOX-1`, layout version 3):

- An 8 KB ring of records at +0x200: `A5 type seq p1 p2 p3 p4 len text`.
- A table of loaded screens at +0x100, so a late-starting companion still knows which screens are up.
- An input queue at +0x40.

**INT 61h** dispatches the hooks. Prologue hooks replay `push bp / mov bp,sp` on the way out.

**INT 1Ch** delivers queued input:

- keys go into the BIOS keyboard buffer;
- clicks are posted into the game's own event queue (press, then release on the next tick), after moving the game cursor.

### `OTAccess.exe` (`src/companion`)

- Starts DOSBox Staging with a generated config (`otaccess_dosbox.conf`):
  - no pausing when the window is inactive;
  - `OTMOD`, then `OREGONM`.
- Finds the mailbox by scanning DOSBox's memory. Guest RAM is one block, so host address = block base + linear address.
- `Mod` (`mod.cpp`):
  - keeps the stack of visible screens and the text on each;
  - turns controls into a list you can move through;
  - names choices from button image frames and nearby text;
  - speaks new text in reading order (paragraphs joined, two-column status page read column by column).
- Travel: holds the event poll (the game freezes) while trail messages are read, in day-by-day mode, or on F8.
- Hunting:
  - tracks animals across frames and measures their speed;
  - leads shots;
  - aim assist moves the bullet's target onto the chosen animal;
  - automatic hunting picks targets itself.
- Rafting: predicts where each rock will be when it reaches the raft and drives the raft by placing the game cursor beside it.
- Map button: the picture is replaced by a spoken route summary.
- Speech goes to SAPI (`SAPI.SpVoice` through automation).
- A low-level keyboard hook takes the keys while the mod window or the DOSBox window is in front.

## Testing done (Linux harness, DOSBox 0.74, with and without the SB16 sound driver)

- New game:
  - party setup, including name entry;
  - store purchases;
  - starting month.
- Trail:
  - continuous and day-by-day travel;
  - event pauses;
  - pace, rations and rest (resting days are announced);
  - status, map description and guidebook;
  - trade and talk.
- Options:
  - save game (file name entry, hidden error boxes ignored);
  - load game.
- River crossings.
- Hunting with automatic hunting and aim assist.
- Rafting:
  - the full Columbia River run with automatic steering and no collisions;
  - the crash screen (Enter continues).
- Willamette Valley arrival, the score screen (including division signs) and the legend name entry.
- The Windows program has been compiled with MinGW (no warnings) but has not yet run on Windows.
