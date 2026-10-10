# Caro Temple — Design System & Screen Specs

Source of truth for every frontend worker. Derived from the PRD (`Caro.pdf`), the UI/UX Pro Max
"Pixel Art" design system, and the pixel-creature style brief (`claude-pixel-creature-style.md`).
Game language: **English by default**; Vietnamese available in Settings (PRD bilingual requirement).

## 1. Look & feel
- **Pixel / retro, temple theme** inspired by Fireboy & Watergirl *Forest Temple*: dark mossy stone
  bricks, gold carved title, rows of red (fire) and blue (water) doors, vines, a soft light shaft.
- **X = fire** (orange/yellow flames), **O = water** (blue ring with rippling highlight). Player 1 is
  always Fire (X) and moves first; Player 2 / the computer is Water (O).
- Hard pixels: draw art with integer-scaled rectangles, nearest-neighbour textures, no blur.
  The VT323 pixel font is used for ALL text (it contains Vietnamese glyphs).
- Each board size has its own background (PRD): 3x3 **Fire Shrine** (warm red brick, lava glow, torches),
  5x5 **Forest Temple** (mossy green, vines, fireflies), 10x10 **Water Sanctum** (blue-grey stone,
  waterfalls / pools, bubbles).

## 2. Tokens (see `gui/theme.h`, `tui/tui.h`)
| Token | Hex | Use |
|---|---|---|
| stone-900 | #181B11 | deepest bg, letterbox |
| stone-800 | #2B2F1E | panel fill |
| stone-700 | #3D4329 | raised fill, buttons |
| stone-600 | #565E3A | borders, bricks |
| stone-400 | #828A60 | brick highlight edge |
| moss / moss-light | #5C7A2E / #8CB048 | vines, success |
| gold / gold-dark | #E8C25A / #967028 | title, primary CTA, selected state |
| fire / fire-light | #FF5A1F / #FFB13B | player 1, X |
| water / water-light | #2FA8FF / #9BE7FF | player 2, O |
| text / text-dim | #F4ECD0 / #B2AA8C | body text (contrast ≥ 4.5:1 on stone-800) |
| danger | #D9452B | surrender, delete, low time |
| focus | #FFFFFF | 2 px outline on hover/focus (PRD) |

Type scale (virtual 1280x720): huge 96 (title), h1 56, h2 40, body 32, small 24. Never below 24.
Spacing: 8 px grid (8/16/24/32/48). Buttons: min 56 px tall, 16 px gap between stacked buttons.

## 3. Interaction rules (UI/UX Pro Max checklist)
- **Hover / focus (PRD):** the label turns **bold** (faux-bold) + **2 px white outline** + lifts 2 px,
  eased over ~150 ms (fill brightens slightly). Same visual for mouse hover and keyboard/gamepad focus.
  Pressed: sinks 2 px for 80 ms. Disabled: 40% alpha, not focusable.
- Every screen is fully usable with mouse **and** keyboard **and** gamepad. Focus always visible; first
  sensible control focused on enter. Esc / gamepad B = back. Gamepad Start / Home key = go Home
  (from a match: open "leave match?" confirmation instead of instantly discarding).
- Bottom hint bar on every screen shows the relevant controls (`ui_hint_bar`).
- **Transitions (PRD):** fade-through-dark 0.35 s between scenes, popups scale 0.92→1 + fade in 0.18 s,
  out faster (0.12 s). All time-based with capped dt so there is no frame hitch. Reduced-motion setting:
  transitions 0.12 s, no screen shake, fewer particles, no idle bobbing.
- Feedback: every action has a sound (hover, click, back, place, error, popup) and a visual response.
  Invalid move (occupied cell) → SFX_ERROR + brief red flash on that cell.
- Toasts (bottom center, 2.5 s) for confirmations: "Saved to slot 3", "Undo declined".
- No emoji anywhere. Icons are drawn pixel shapes or text glyphs from VT323.

## 4. Mascot (option A — chosen by the user)
The 16x16 blocky creature from the style brief (same body for all), coloured like the PDF's elemental twins.
Reference render: `docs/mockups/mascot_mockups.png`, generator: `tools/mascot_mockup.py` (grids + palettes
below are authoritative; port them to C as string arrays in `gui/art.c`).

Body (rows 6-15; `.` transparent, B body, S shade, H highlight, E eye):
```
...HBBBBBBBBB...
...BBBBBBBBB....
..BBBEBBBEBBB...
...BBEBBBEBB....
...SSSSSSSSS....
...S.S...S.S....
...S.S...S.S....
...S.S...S.S....
................
................
```
Headgear (rows 0-5; A accent, L/Y accent-light, F accent, H highlight):
```
flame (avatar 0)   drop (avatar 1)    sprout (2)        crown (3)         helmet (4)        antenna (5)
.......Y........   ..........L.....   ................  ................  ................  .......L........
......YF...Y....   .........LL.....   .........AA.....  ....A..A..A.....  .......A........  ......LLL.......
.....YFF..YF....   ........LAL.....   ....AA..AAA.....  ....AA.A.AA.....  .....AAAAA......  .......A........
....YFFLFYFF....   .......LAAL.....   ....AAA.A.......  ....AAAAAAA.....  ....AAHAAAA.....  .......A........
....FFLLLLFF....   ....LLLAAL......   ......AAA.......  ....AHAHAHA.....  ...AAAAAAAAA....  .....AAAAA......
...FFLLLLLLFF...   ...LAAAAALL.....   .......A........  ....AAAAAAA.....  ...A.A.A.A.A....  ................
```
After compositing, add a 1 px outline (4-neighbour) around every filled pixel in the outline colour.

Palettes (outline, body, shade, highlight, eye, accent, accent-light):
| Mascot | colours |
|---|---|
| 0 Fire (flame) | #4A1408 #E8562A #B8361A #FFB13B #2A0E06 #FF7A1F #FFE066 |
| 1 Water (drop) | #0B2747 #2FA8FF #1A72C4 #C8F2FF #0B1F33 #2FA8FF #9BE7FF |
| 2 Forest (sprout) | #1E2A0E #7FA34A #5C7A2E #C6E08A #16200A #4FBF3A #B8F07A |
| 3 Temple King (crown) | #5A2A18 #D97757 #B85C3D #F4B89C #1F1E1D #F2C14E #FFE9A8 |
| 4 Guardian (helmet) | #1A1C12 #8A8F70 #656A50 #C8CCAE #1A1C12 #B9B79C #E8E6CF |
| 5 Automaton (antenna) | #121110 #3D3A36 #2A2825 #6B6660 #F0A27E #C9A227 #E8D27A |
| 6 Novice (hood), computer Easy | #4A3A1E #D8C08A #B39A62 #F4E6BE #2A2014 #8C6A3A #B08850 |
| 7 Obsidian Warden (horns), computer Hard | #120A1E #3B2A5A #2A1D42 #6E58A0 #FF4FD8 #E8C25A #FF6FE0 |

### Computer champions (vs Computer)
Each difficulty has its own opponent; players cannot pick these avatars (6, 7):
Easy → **Novice** (avatar 6), Medium → **Automaton** (avatar 5), Hard → **Obsidian Warden** (avatar 7).
Name `tr(S_CHAMP_EASY + diff)`, title `tr(S_CHAMP_EASY_TITLE + diff)`, avatar `game_champion_avatar(diff)`.
Headgear grids (rows 0-5) for the two new champions:
```
hood (6)           horns (7)
................   ................
................   ...A.......A....
......AAAA......   ...AA.....AA....
.....AALLAA.....   ....AA.L.AA.....
....AALAAAAA....   .....AALAA......
...AAAAAAAAAA...   ....AAAAAAA.....
```
The Warden's eyes glow (magenta, subtle pulse); Novice blinks like the others.
Reference render: `docs/mockups/champions.png`.

Animation: blink every 3–5 s for 0.12 s (eye top pixel → body colour, bottom → shade); idle bob of 1 art-pixel
at ~1 Hz; fire twin's flame tips flicker between accent and accent-light every ~0.15 s; water twin's droplet
shimmers. Mascots never get a mouth, arms, logos or text.

## 5. Screens (virtual 1280x720)
### Home (`scr_home.c`, BG_HOME)
- Title "CARO TEMPLE" (art_draw_title) centered at y≈70, subtitle "Fire vs Water" gold-dark below.
- Centered vertical menu, buttons 360x60, starting y≈260: **Play** (primary/gold), **Continue** (only
  enabled if a match is in memory or a save exists — resumes the in-memory match, else loads the latest slot),
  **Load Game**, **Settings**, **Quit** (danger style, asks confirmation popup).
- Fire mascot bottom-left (≈ x 120, y 520, 160 px), Water mascot bottom-right, idle animated, like the PDF
  menu's Fireboy/Watergirl corners.
- Hint bar: "Arrows/WASD: move · Enter/Space: select · Esc: back".

### New Game setup (`scr_setup.c`, BG_MENU)
- Panel titled "New Game", width ~760, centered.
- Mode: two large selectable cards side by side ("vs Computer" with Automaton mascot, "2 Players" with
  fire+water mascots), selected card has gold border.
- Board size: three cards 3x3 / 5x5 / 10x10 each with a tiny preview of its background colours and the win
  rule ("3 in a row" / "5 in a row").
- Difficulty (only when vs Computer): Easy / Medium / Hard segmented choice.
- Toggle: "Blocked ends don't count" (only for 5x5 / 10x10) with one-line description.
- Buttons: Back, **Next** (primary) → Profile. Choices persist in settings.last_*.

### Profile (`scr_profile.c`, BG_MENU)
- PvE: one player card. PvP: two cards side by side (Player 1 "Plays Fire (X)" with fire-tinted border,
  Player 2 "Plays Water (O)" water-tinted).
- Each card: avatar portrait 160 px (art_draw_avatar), "< >" avatar choice (6 mascots), name text box
  (max 16 chars, placeholder "Enter name"). Empty name → inline red error under the field, Start disabled.
- Buttons: Back, **Start** (primary). Saves to settings.profile[], then app_start_new_game().

### Settings (`scr_settings.c`, BG_MENU, pushed over Home or Game)
- Panel "Settings": Language choice (English / Tiếng Việt, applies immediately), Music volume slider,
  Music mute toggle, Sound FX slider, SFX mute toggle, Reduced motion toggle, Fullscreen toggle.
- Right column (or second panel): Controls reference (mouse, P1 keys, P2 keys, menu keys, gamepad).
- Back saves (app_save_settings) and scene_pop().

### Saves (`scr_saves.c`, BG_MENU, pushed; mode from g_app.saves_mode)
- Title "Load Game" or "Save Game". 10 slots as a 2x5 grid of stone cards (each ~520x88): "Slot N",
  date/time, mode + board size + difficulty, player names, "N moves"; empty → "Empty slot" dimmed.
- Load mode: empty slots disabled; confirm loads. Save mode: confirm on used slot → overwrite popup.
- Delete via secondary small button on a used card (or X / Delete key) with confirmation.

### Game (`scr_game.c`, board bg per size)
- Layout: board slab centered-left, square, max 600 px (cell = 600/n; 3x3 cells 180 px, 10x10 cells 60 px),
  top-left at x≈ 340, y≈ 80. Left HUD card (x 24, w 280) = Player 1; right HUD card (x 976, w 280) = Player 2.
- HUD card: avatar 96 px, name, symbol (fire X / water O mini), **clock mm:ss** (big, turns danger red and
  pulses under 60 s; SFX_TICK each second under 10 s), active player card glows in its colour; "Thinking…"
  under the AI while it computes (AI waits 0.35–0.6 s for feel).
- Top bar: "Started 14:05" (wall-clock time at match start, PRD) + elapsed mm:ss + turn banner
  "Fireboy's turn". Bottom: Undo, Surrender, Menu buttons + hint bar.
- Board: hover/cursor cell shows ghost piece of the current player; keyboard cursors: P1 fire-coloured
  corners (WASD + Space), P2 water-coloured corners (arrows + Enter); in PvE either key set moves the human's
  cursor; mouse click places. The **opponent's latest move is always highlighted** (fx_draw_last_move).
- Placement: piece pops in (appear anim 0.25 s) + particle burst + SFX_PLACE_FIRE/WATER.
- Win: glowing win line across the run, celebration particles, then the Game Over popup after ~0.8 s.

### In-game overlays (`scr_overlays.c`)
Popups (ui_popup) over a dimmed game:
- **Pause** (Esc / Menu button / gamepad B): Resume, Save Game, Undo, Settings, Surrender, Home.
  Clocks stop while paused.
- **Undo request** (PvP): "<Name> asks to undo the last move. Accept?" — Accept / Decline (answered by the
  opponent). Toast result. In PvE undo applies instantly (toast "Move undone").
- **Surrender confirm**: "<Name>, surrender this match?" Yes (danger) / No.
- **Leave confirm** (Home): keeps the match in memory so Continue can resume it.
- **AFK** (after 2 min no input): "Still there?" / "Do you want to keep playing?" with a big countdown
  (30→0) and a draining bar; **Yes** continues. At 0: autosave to a free slot (or oldest), toast
  "Auto-saved to slot N", go Home.
- **Game Over**: big "Fireboy wins!" (in the winner's colour) or "Draw", reason line (five in a row /
  time ran out / surrender), winner's mascot bouncing; Rematch (primary) / Home.

## 6. Terminal version (`tui/`)
Same flows and features as the GUI, no audio, no gamepad. Min 80x30. Board cells 4 cols x 2 rows on
3x3/5x5 (larger cells), 4x2 or 3x1 on 10x10 to fit. Fire X drawn as bold `╳`/`X` in fire colours with a
flicker between fire / fire-light; water O as `◯`/`O` in water colours. Last move: gold background cell.
Cursor: reverse video. Panels with rounded box borders in stone/gold. Mouse click selects cells & buttons.
