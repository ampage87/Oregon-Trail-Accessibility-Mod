# Oregon Trail Accessibility Mod

Makes The Oregon Trail Deluxe (DOS, version 3.0, 1992) playable without vision. Speech comes from Windows SAPI voices. The game runs unchanged in DOSBox Staging. A small helper program inside DOSBox reports what the game draws. The Windows program, OTAccess.exe, reads it aloud and plays the game for you through the keyboard.

## Installing

1. Download `OregonTrailAccessibilitySetup` from the [Releases page](https://github.com/ampage87/Oregon-Trail-Accessibility-Mod/releases) and run it. No administrator rights are needed.
2. Setup does the following:
   - downloads The Oregon Trail Deluxe for DOS from the [Internet Archive](https://archive.org/details/msdos_Oregon_Trail_Deluxe_The_1992), or copies a game folder you already have;
   - downloads [DOSBox Staging](https://www.dosbox-staging.org/), which runs the DOS game on Windows;
   - installs the mod into your user profile (by default `%LOCALAPPDATA%\Programs\Oregon Trail Accessibility Mod`);
   - offers a Start menu shortcut, a desktop shortcut, and starting the game right away.
3. The game itself is never part of this project or its downloads. The original `OREGON.EXE` is never changed; the mod makes a separate hooked copy called `OREGONM.EXE`.

The mod needs The Oregon Trail Deluxe for DOS, version 3.0 (`OREGON.EXE`, 273,648 bytes). Setup checks this and tells you if a different version was found.

## Playing

- Turn your screen reader off, or put it to sleep, while you play. The mod speaks with SAPI and needs the keys a screen reader would otherwise take.
- Start the game from the shortcut Setup made, or run `OTAccess.exe`. It opens a small window and starts DOSBox. You can play with either window in front.
- The mod window holds a history of everything spoken. Press F10 to read it with the arrow keys, and F10 again to return to the game.
- Closing the mod window also closes the game.

### Keys everywhere

| Key | What it does |
|---|---|
| Up, Down, Tab, Shift+Tab | Move between the choices on the screen |
| Home, End | First or last choice |
| Enter or Space | Choose. On a text box, starts typing (a rising tone); Enter or Escape finishes (a falling tone). Typing a number on a text box starts typing straight away |
| Escape | Cancel, Done or No |
| Letters | Jump to the next choice starting with that letter |
| F1 | Help for the current screen. Shift+F1 lists every choice |
| 1 to 9, 0 | On the trail: one part of the status at a time (see below) |
| F2 | Conditions: date, weather, health, food, pace, rations, wagon, distance |
| F3 | Party members and their health |
| F4 | Supplies and money |
| F5 | Distance to the next landmark |
| F6 | Read everything on the screen |
| F7 | Latest trail log messages. Shift+F7 reads the whole log |
| F8 | Pause or resume travel. In day-by-day mode, travel one more day |
| F9 | Travel mode: continuous, day by day, or never pause |
| F10 | Switch between the game and the speech history |
| F11 | Daily reports on or off |
| F12, Shift+F12 | Speech faster or slower |
| Ctrl | Stop speech |
| Ctrl+R | Repeat the last thing said |

### Trail screen shortcuts

| Key | Action |
|---|---|
| C | Continue on the trail, or time out |
| S | Status |
| M | Map (spoken as a description of the route ahead) |
| G | Guidebook |
| A | Food rations |
| P | Pace |
| R | Rest |
| B | Buy supplies |
| T | Trade |
| K | Talk to people |
| H | Hunt |
| O | Options |

When a small window opens over the trail (pace, rest, trade, guidebook), its own choices come first and the trail buttons follow them. Letters jump to that window's choices first.

### General store

- Each amount you enter is followed by the item's cost, the running total, your money, and what will be left.
- Press T or F2 at any time to hear the total, your money and what will be left.

### Rest

- R opens Rest with the cursor already in the days box. Type the number of days and press Enter; that also confirms.

### Status on the number keys

These work anywhere on the trail, with or without the Status page open. Opening Status no longer reads the whole page.

| Key | Reads |
|---|---|
| 1 | Date and weather |
| 2 | Each person's health |
| 3 | Food, rations, and about how many days it will last |
| 4 | Oxen |
| 5 | Clothing and bullets |
| 6 | Spare wheels, axles and tongues |
| 7 | Money |
| 8 | Pace, rations, health and wagon status |
| 9 | Distance to the next landmark |
| 0 | Everything |

### Messages and the guidebook

- When a screen reads a passage (a guidebook page, river information, help), the passage is offered as the first choice, named "Page" or "Message". Press Enter on it to hear it again.
- A guidebook page opens with focus on its text.

### Travel

- When something happens on the trail, travel pauses while the message is read, then carries on. Each log message is read once, with its date.
- In day-by-day mode (F9), travel stops after each day. Press F8 for the next day.
- While resting, each day is announced.

### Lists (guidebook index, saved games, legends)

- The mod reads every entry in the list straight from the game, so the arrow keys move through the whole list instantly, and each entry is announced with its position.
- Typing a letter jumps to the next entry starting with that letter.
- Tab moves past the whole list to the next button.
- In the guidebook index and the load-game list, Enter opens the entry. Space only selects it.
- The guidebook index opens on the topic you were last reading.

### Hunting

- Animals are announced as they appear: kind, side, distance and direction.
- Left and Right choose an animal. Space shoots at it, leading the shot, and aim assist steers the bullet.
- A starts automatic hunting, which skips rabbits and squirrels. L lists the animals. M moves to a new area. E ends the hunt.
- Hunts last 120 seconds instead of the game's 60. Change this under Options in the game, or in `OTAccess.ini`.

### Rafting down the Columbia

- You hear running water for the whole run, so you know rafting is still going.
- Automatic steering is on by default and avoids the rocks. It says "Rock ahead, steering left" or "right" as it dodges.
- Progress is announced about every 15 seconds. Enter tells you how far along you are at any time.
- Press A to steer yourself with Left and Right. Each press moves the raft about a raft's width, and rocks are announced with the direction to steer.

## Settings (OTAccess.ini)

- `speech.rate` sets the voice speed, from -10 to 10.
- `speech.voice` picks a voice by part of its name, for example `Zira`.
- The `[sound]` settings turn the river sound and the text box tones on or off, and set their volume.
- Travel, hunting and rafting options are described in the file itself.
- `dosbox.path` is only needed if DOSBox Staging is not found automatically.

## Building from source

You need Windows with Visual Studio (the "Desktop development with C++" workload).

1. Clone the repository.
2. Put the game files in a folder named `Game Files` next to `deploy.vbs`.
3. Install DOSBox Staging, for example with `winget install DOSBoxStaging.DOSBoxStaging`.
4. Run `deploy.vbs`. A progress window reads out each step and ends with a message box. If something fails, a dialog offers "View Build Log", and the log is saved in `Logs\build_latest.log`.

The deploy:

- builds `OTAccess.exe`;
- copies `OTMOD.COM` into `Game Files` (it is rebuilt from `src\dos\otmod.asm` if NASM is installed, otherwise decoded from `bin\OTMOD.COM.b64`);
- creates `OREGONM.EXE` and `OTAccess.ini`.

Every push to `main` runs the GitHub Actions workflow in `.github/workflows/build.yml`. It builds the program, the DOS helper and the Inno Setup installer (`installer\OTAccessSetup.iss`), and attaches the installer to the release named after the `VERSION` file. To publish a new release, change `VERSION` and push.

## Files

| Path | What it is |
|---|---|
| `deploy.vbs` | Build and install from source |
| `installer\` | Inno Setup installer script |
| `.github\workflows\` | Automatic builds and releases |
| `OTAccess.exe` | Start the game with the mod |
| `src\companion\` | Windows program source (C++) |
| `src\dos\otmod.asm` | DOS helper source (NASM). `bin\OTMOD.COM.b64` is a prebuilt copy |
| `tools\` | Python tools used for testing and patching |
| `Plan & Research Documents\` | How the game works inside, and how the mod hooks it |
| `Logs\` | Build log, and the mod's run logs: `otaccess.log` for the latest session and `otaccess_previous.log` for the one before |

## License

The mod's code is under the MIT license (see `LICENSE`). The Oregon Trail is a trademark of its owners; this is an independent fan project, not affiliated with or endorsed by them. DOSBox Staging is free software under the GNU GPL.
