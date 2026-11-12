# Accessibility Framework: Initial Architecture

## Purpose

Build a maintainable accessibility layer for Cataclysm-DDA, beginning with Windows screen-reader support and expanding to consistent keyboard navigation, braille where supported, and accessible presentation across the game's UI.

The accessibility layer must not depend on screen readers scraping the pixels or text rendering of individual UI windows. Game systems and UI components should publish semantic events and structured content; accessibility services should present that information through the available output backends.

## Baseline

This branch is based on upstream commit `c337a0ca871cf0908deb41a0110f44482825a811`, selected to match the version identified by the accessible Windows build's VERSION.txt.

The accessible build's original source repository is not available. Its executable and bundled DLLs are reference material, not the source of truth for this implementation.

## Architectural principles

1. **One semantic source, multiple presentations.** Keep game data and meaning separate from visual formatting, speech, and braille formatting.
2. **Centralize output.** UI code must not call NVDA, SAPI, or vendor-specific APIs directly. It submits narration requests to one accessibility service.
3. **Explicit UI events.** Opening a dialog, changing selection, updating a description, receiving a message, and moving through the world are distinct events.
4. **Predictable focus.** Each accessible UI exposes its current focus/selection and an appropriate description. Focus changes should produce deliberate announcements, not accidental redraw narration.
5. **User control.** Speech verbosity, interruption, repetition, automatic announcements, and backend choice must be configurable.
6. **Graceful degradation.** If a screen reader or speech backend is unavailable, the game continues normally and can use another configured backend.
7. **No visual regression.** Accessibility behavior should be additive and should not impose unwanted changes on players who do not enable it.
8. **Testable boundaries.** Core narration decisions should be testable without launching the game or requiring a running screen reader.

## Proposed components

### Accessibility service

A central service owns initialization, shutdown, backend selection, speech queue policy, interruption, repeat-last-announcement, and capability reporting.

### Output backends

Backends implement a small common interface for capabilities such as speaking text, cancelling speech, and displaying braille when supported. Initial Windows candidates are NVDA Controller Client, System Access, and SAPI. The implementation must verify the actual API contracts and licensing/deployment requirements before choosing dependencies. Screen-reader detection and speech synthesis should remain separate concepts.

### Semantic narration

UI and game code submit typed events rather than preformatted window text. Initial event families:

- UI opened or closed
- Focus or selection changed
- Item description opened or changed
- Status changed
- Game message received
- Player movement and surroundings changed
- Contextual help requested

Each event carries structured data and enough context to determine whether it should be spoken, queued, suppressed, or repeated.

### Structured content

Where the game already has structured data, reuse it. Item descriptions should be derived from item information entries, not scraped from ImGui or curses rendering. The visual, speech, and braille presenters should consume the same semantic content while applying their own formatting.

### UI accessibility context

An accessible UI should be able to report:
- Its identity and role
- Its current focus or selected entry
- A concise label for that focus
- A detailed description when requested
- Available navigation actions
- Whether it is modal and how it is dismissed

This should be introduced incrementally rather than requiring a wholesale rewrite of every existing UI.

## First target: item information

The first end-to-end feature is the inventory item description.

Expected behavior:
1. Opening an item's description announces the item name and a useful summary.
2. The full description is available without relying on OCR or visual text extraction.
3. Scrolling produces sensible announcements without repeating the entire description unnecessarily.
4. A repeat command repeats the last relevant narration.
5. Closing the description returns focus to the inventory context.
6. The feature works independently of whether the UI is rendered through ImGui or curses.

## Implementation sequence

1. Audit UI lifecycle, input contexts, item information structures, options, and build configuration on this pinned baseline.
2. Define the backend-neutral accessibility service and event model.
3. Add a minimal testable speech backend interface and a no-op/test backend.
4. Integrate one Windows backend and verify initialization, speech, cancellation, and shutdown.
5. Connect the item-information path end to end.
6. Extend the same event model to inventory selection, menus, messages, status, and world navigation.
7. Add settings, documentation, regression tests, and packaging checks.

## Constraints and open questions

- Do not assume that ImGui's rendered text is exposed to Windows UI Automation or NVDA. Verify actual behavior separately.
- Do not assume the accessible reference executable statically or dynamically links Tolk until the binary evidence establishes that.
- Determine the project's supported compiler, Windows toolchain, and dependency conventions before adding a third-party library.
- Confirm which backend APIs are available in the target build and how their DLLs are packaged.
- Keep the existing game UI operational if accessibility initialization fails.

## Current evidence

The inventory examination path constructs an `iteminfo_window`. That window renders the item name/type and delegates the description to `display_item_info()`, then waits for input in its execution loop. The UI adaptor documentation describes terminal cursor recording for screen readers as curses-only and notes that the current tiles implementation does not support screen readers through that mechanism.

This supports an architecture in which narration is driven by semantic UI events and structured item data, rather than by terminal cursor placement or rendered-window scraping.

## Implemented prototype on this branch

The first prototype now contains:
- A backend-neutral `narration_service` with semantic event identifiers, last-announcement storage, repeat, cancellation, and optional braille forwarding.
- A Windows NVDA Controller Client backend loaded dynamically from the executable's own directory. It checks whether NVDA is running before sending speech or braille, and supports the legacy architecture-suffixed DLL names as well as the current unsuffixed name.
- A plain-text item-information adapter that reads the existing `iteminfo` entries rather than scraping rendered UI text.
- An integration point in the inventory selector's `EXAMINE` action, which submits an `item_description_opened` event.
- Unit tests for service behavior and item-information text conversion.

The NVDA backend uses the documented Controller Client functions for testing whether NVDA is running, speaking text, cancelling speech, and displaying a braille message. See the [NVDA Controller Client API documentation](https://github.com/nvaccess/nvda/blob/master/extras/controllerClient/readme.md).

## Prototype limitations

This is an early vertical slice, not a complete accessible game yet:
- The NVDA DLL must be present beside the executable. The prototype does not download or install it.
- NVDA is the only implemented output backend so far. System Access and SAPI fallback are not implemented yet.
- Braille forwarding is available through the service but has not yet been connected to automatic UI events.
- The inventory description is currently announced when opened. Scroll-position announcements and focus restoration still need implementation. A repeat command is available in the inventory and item-information window on F7; F6 describes the current terrain underfoot during gameplay. Ctrl+R is already assigned to a debug-dialogue action in the upstream default keybindings.
- The tests have been added to the project's Catch-based test suite, but a Windows build and live NVDA test have not yet been run from this environment.
- Narration requests are forwarded immediately. A managed queue, verbosity controls, and event-specific announcement policies remain future work.

### Gameplay keyboard bindings

In DEFAULTMODE, Ctrl+L surveys adjacent tiles (F8 alternative), Ctrl+S speaks a status glance, F9 opens the status browser, and Ctrl+R reads the five most recent messages (F10 alternative). The glance and browser both come from `src/sidebar_accessibility.cpp`, which walks the player's active sidebar layout (`panel_manager`) widget by widget and turns each one into a spoken row, so the screen-reader player hears the same sections, order and vocabulary as a sighted player with the same layout. The browser itself (`src/status_browser.cpp`) is a `uilist` with one category per panel and its own `STATUS_BROWSER` keybinding context (F8 read section, F1 read all, Tab/Shift+Tab switch sections). These controls are deliberately scoped to gameplay: Ctrl+R continues to belong to the dialogue debug action when that interface is active, and Ctrl+S keeps its string-editor confirmation behavior. Ctrl+F speaks a description of the scene in view; it comes from `src/scene_description.cpp`, where `gather_scene` scans the tiles the avatar can currently see into a plain `scene_snapshot` and `render_scene` turns that snapshot into speech, so the renderer is unit tested without a map. F6 describes terrain underfoot; F7 repeats the most recent narration; F11 toggles optional terrain change cues.
