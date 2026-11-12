# NVDA accessibility guide for Cataclysm: Dark Days Ahead

This fork aims to make the ordinary CDDA game accessible through NVDA. It does **not** automatically explore, fight, or make survival decisions for the player.

## Opening this guide

- **F12**: Read the essential accessibility controls aloud while playing. The spoken guide is available in the normal gameplay screen.
- **?**: Open CDDA's ordinary help or keybinding controls. The in-game keybindings menu is the authority if you have remapped keys.

## Exploring the world

- **Shift+V**: Open the list of nearby monsters, items, terrain, and furniture. This is the key surroundings scanner; use the list's controls to browse the categories.
- **E** in the Items tab of Shift+V: Examine the selected item. NVDA announces its detailed item properties and description as the item information window opens. This does not yet add a spoken Examine action to the Creatures or Terrain/Furniture tabs.
- **x** (lowercase): Open the look-around cursor. Move the cursor with directional keys to examine visible or otherwise legitimately known tiles.
- **Shift+X**: Peek around a corner. Select a direction and use the look-around cursor from the temporary peeking viewpoint. Peeking costs moves. The dedicated NVDA explanation of the peeking transition is still incomplete.
- **Ctrl+F**: Describe the scene in view. Speaks, in order: indoors or outdoors and the kind of place, the light on your tile, hostile creatures grouped by kind and direction with whether they see you, friendly and neutral creatures, how far you can go in each of the eight directions and what stops you, the nearest doors, windows and stairs, any visible hazards such as fire or spotted traps, then furniture, vehicles, and how many tiles hold items. Costs no game time.
- **Ctrl+L** or **F8**: Describe the eight tiles adjacent to your character.
- **F6**: Describe the tile underfoot.
- **F11**: Toggle automatic announcement when the terrain underfoot changes. Off by default to avoid needless narration.

## Putting items into cupboards and other furniture

- **Shift+D** (capital D): Drop items onto a neighboring tile. Stand beside a cupboard, shelf, locker, or other suitable storage furniture; press Shift+D, choose the furniture's direction, then select the items to place there and confirm.
- **/** (slash): Open Advanced Inventory Management. This lets you transfer items between your inventory and nearby tiles, including storage furniture.
- Most CDDA cupboards and shelves are furniture tiles with storage capacity, not containers you need to open first. Placing items on their tile stores them there. Ordinary **d** drops items at your position instead; **Shift+D** selects an adjacent destination.
- The usual keybindings can be changed. Open **?** to inspect or remap controls if these keys behave differently.

## Adjacent drop, advanced inventory, and spell targeting

- **Shift+D**: The adjacent-drop command announces "Drop where?" and asks for a direction. After choosing a tile, CDDA opens the normal item selection interface. Use it to select which items to drop on a cupboard, shelf, or other valid tile.
- **/**: Advanced Inventory Management announces the active left/right pane, its location, and the selected item as focus changes. Navigate and move items using the normal CDDA advanced-inventory controls; use **?** in that screen to review bindings.
- **]**: When casting a targeted spell, the spell-targeting interface announces its instructions and the selected creature or tile, cursor offset, and distance. The narrator reports the game's known target, not hidden creatures or unexplored terrain.
- These are source changes and require an NVDA build test to verify speech timing and that all relevant targeting modes work.

## Body status and unloading containers

- **{** (left curly brace, usually Shift+[ on US keyboards): Open Body Status. NVDA announces the selected body part, health, temperature, effects, worn items, coverage, encumbrance, and protective statistics as you change the selected part.
- **Backspace** (and potentially Shift+Backspace, depending on the input system): Unload a container onto the ground. NVDA announces the destination-direction prompt before the regular item-selection window opens. This differs from **U**, which unloads into inventory.
- These are the current default bindings; use **?** to check remapped controls.

## Death and game-over narration

- The final tombstone screen announces itself and explains that Enter continues. If a last-words input is present, it can be edited before continuing.
- The game-over flow announces final messages, scores, epilogue checks, and completion. The scores window narrates its tab name and up to twelve entries when entering the Achievements, Conducts, or Scores tabs; the Kills tab announces totals. Use left and right arrows to change tabs and Escape to continue.
- The main menu retains its own spoken entry and focused selection after the game returns to it. These changes need an NVDA playtest to verify timing and focus.

## Loading saved characters and finding Abandon Character

- **Load Game → choose a world → choose a character**: selecting a saved character loads it directly. There is no custom character Delete/archive action; no `.deleted` files are created by the accessibility load menu.
- **Abandon Character** now always appears in the gameplay action menu even without a keyboard shortcut. It is the game's original death/abandon action, not the safe character-removal option.
- **View Help** announces its help topic shortcuts, then narrates the chosen help article.
- **Sidebar Options** announces focused panel or layout, including whether a panel is enabled. Tab toggles a panel, Enter or S begins/ends moving a panel or selects a layout, and left/right switches columns.

## Random world and character selection

- **Shift+R in the world picker during character creation**: Choose one existing, eligible world randomly. Tutorial and defense worlds are excluded by the ordinary picker. If there is only one eligible world, CDDA selects it automatically; with none, the ordinary create-world flow applies.
- **Shift+R on the main menu under Load Game**: Pick a random saved character from any normal world, announce the chosen character and world, and ask for confirmation before loading.
- **Random saved character** is also a selectable entry at the bottom of an individual world's Load Character list. This chooses only among that world's saved characters, and asks before loading.
- These commands select existing worlds or characters. They do not create new random worlds, delete saves, or change a character's progress.

## World mod selection

- **Shift+R** in the world mod manager: Propose a random selection of two to five optional mods. The selected core mod is preserved; the game attempts to resolve dependencies and avoid declared conflicts.
- NVDA announces the proposed mod list and asks for confirmation **before** replacing your existing selection. Declining leaves it unchanged.
- The mod manager now reads each mod's description, dependencies and declared conflicts when browsing. Authors may not declare every incompatibility. For major total conversions, consider separate worlds.
- This feature changes only the world's mod selection during setup. It does not explore or play the game automatically.

## Status and messages

- **Ctrl+S**: Status glance. Speaks the creatures in view (by direction, and whether a hostile can see you), then only the sidebar values a sighted player would see highlighted: wounded limbs, low stamina, hunger or thirst, heavy load, and so on. Says "All sidebar values normal" when nothing is out of the ordinary.
- **F9**: Status browser. Opens the whole sidebar as a spoken list, one section per sidebar panel in the same order and with the same panels you have enabled in the sidebar options: Limbs, Movement, Stats, Weariness, Needs, Place, Wind/Temp, Weapon, Vehicle, Compass, Weight and the message Log. **Up/Down** read one row at a time ("Left arm: 45 of 60, bleeding lightly"), **Left/Right** or **Tab/Shift+Tab** switch sections, **F8** reads the current section, **F1** reads every section, **F7** repeats, **Escape** closes. It reopens at the section you last visited.
- **Ctrl+R** or **F10**: Speak the last five game messages.
- **F7**: Repeat the previous NVDA announcement.

## Important distinctions

**Shift+V** is the surroundings list; lowercase **v** is not the same command. **x** and **Shift+X** are different: looking around and peeking around a corner, respectively. **Ctrl+X** is not the accessibility look command. Capital letters in CDDA often represent Shift-modified commands; this guide explicitly writes out Shift when it matters.

Descriptions must respect the game's visibility and knowledge rules. Unexplored or unseen terrain should not be revealed through narration. Normal CDDA conveniences such as its existing auto-travel remain part of the game, but this fork does not add automatic survival assistance.

## Testing and reporting a freeze

If a tester encounters a hang or silent interface, please report the build version or commit, the steps taken, whether the whole game stopped or only NVDA speech, and whether NVDA was running. The diagnostic file **nvda-accessibility-diagnostics.txt** is beside **cataclysm-tiles.exe**. Copy it **before restarting the game**, because it is overwritten at startup. If present, **debug.log** or **crash.log** may also be helpful.

The guide describes the default bindings for this accessibility branch. Keyboard remaps and future game changes may alter them.
