# Copilot Instructions for Void_OS-Cyber-B.O.Y

## Build & Deployment

### PlatformIO Commands
- **Build firmware**: `pio run`
- **Build & flash to device**: `pio run -t upload`
- **Monitor serial output**: `pio device monitor` (or via PlatformIO extension)
- **Clean build**: `pio run -t clean`
- **Verbose output**: `pio run -v`

**Device**: ESP32-S3-DevKitC-1 with ILI9488 480×320 display (16 MB flash)

## High-Level Architecture

This is a modular, event-driven OS firmware for a multi-function "Cyber B.O.Y" handheld device (radio, NFC, IR, etc.).

### Core Flow
1. **Main loop** (main.cpp) runs at 30 FPS
2. **Event system** collects input events (buttons, potentiometer) and broadcasts them
3. **Current app** processes events and updates its state
4. **UI layer** renders all visible elements each frame
5. **Power/HAL** manages hardware transitions (dimming, sleep, deep sleep)

### Layered Structure
```
┌─ main.cpp (app registry, 30 FPS event loop)
├─ UI layer (UI/draw.h, transitions, theme)
├─ Apps (src/Apps/app_*.cpp - HOME, STAT, RADIO, NFC, IR, SYS)
├─ OS layer (events, scheduler)
├─ HAL (hardware abstraction - display, input, storage, power)
└─ config.h (all GPIO, timing, color defines)
```

### App Lifecycle
Apps register lifecycle callbacks in the app registry:
- **init()**: One-time setup when app loads
- **tick()**: Called each frame (30 Hz) for logic updates
- **draw()**: Called each frame to render
- **event()**: (Optional) Handles custom input events
- **suspend()**: Called when leaving app
- **resume()**: Called when returning to suspended app

### Event System
- Events are pushed by HAL (button presses, potentiometer changes)
- Main loop pops events and distributes them to current app
- Button B always returns to HOME app
- See `os/events.h` for event types

## Key Conventions

### HAL (Hardware Abstraction)
All hardware access goes through `hal_*.h` modules:
- `hal_display_*()` – ILI9488 display via TFT_eSPI library
- `hal_input_*()` – Button (3-button) and potentiometer input
- `hal_power_*()` – Backlight PWM, dimming, sleep management
- `hal_storage_*()` – NVS key-value storage (namespace `"cyberdeck"`)

HAL functions are called from the main loop, not directly from apps. Apps work with events and coordinate through the OS scheduler.

### Drawing API
**Primitive layer** (low-level, pixel-based):
- `draw_pixel()`, `draw_hline()`, `draw_vline()`
- `draw_rect()`, `draw_fill()`, `draw_circle()`, `draw_fcircle()`, `draw_line()`
- `draw_text()`, `draw_textf()` (formatted text)

**Component layer** (higher-level UI building blocks):
- `draw_bar()` – progress/stat bars
- `draw_sprite()` – sprite rendering with transparency

Use RGB565 color format (see `config.h` for predefined colors like `C_PGREEN`, `C_BLACK`).

### Screen Layout
Defined in `config.h`:
- **Resolution**: 480×320
- **Top bar (STATS)**: 28 px high (displays vitals, time, etc.)
- **Left/right panels**: 186 px wide (app-specific panels)
- **Center area**: 108 px wide (character sprite typically rendered here)

### Config & Storage
- All hardware GPIO, timing constants, colors, and app IDs are in `src/config.h`
- Never hardcode GPIO pins or timing values—define them in `config.h` first
- Persistent storage uses NVS with namespace `"cyberdeck"` (see `config.h` for key names)
- Load/save state via `hal_storage_get_u8()`, `hal_storage_set_u8()`, etc.

### Naming Conventions
- **Static module state**: Prefixed with `_` (e.g., `static uint8_t _sel`)
- **App-local state**: Declare static at file scope (not global)
- **Functions**: `<subsystem>_<action>` (e.g., `hal_power_activity()`, `app_home_tick()`)
- **Enums/defines**: `SCREAMING_SNAKE_CASE` (e.g., `APP_HOME`, `EVT_BTN_A_DOWN`)

### Frame Timing & Responsiveness
- Main loop runs at exactly 30 FPS (frame = 33.3 ms)
- `hal_input_tick()` updates smoothed potentiometer each frame
- Buttons are debounced by HAL; events fire on transition
- Keep `tick()` functions lightweight—no long blocking operations
- Use state machines within apps for multi-step operations

### App Data Persistence
- Save important app state to NVS before sleep/suspend
- On init, restore from NVS if available
- Example: HOME app stores `NVS_LAST_APP` to remember which app was active

## Common Tasks

### Adding a New App
1. Create `src/Apps/app_myapp.cpp` with `app_myapp_init()`, `app_myapp_tick()`, `app_myapp_draw()`
2. Add declarations in a new `app_myapp.h` header
3. Register in main.cpp's APPS array (add name, init/tick/draw/event/suspend/resume function pointers)
4. Add `#define APP_MYAPP X` to `config.h` where X = next available app index
5. Update `APP_COUNT` in `config.h`

### Responding to Button Input
Apps receive button events via the `event()` callback:
```cpp
void app_myapp_event(Event e) {
    if (e.type == EVT_BTN_A_DOWN) { /* handle A press */ }
    if (e.type == EVT_BTN_C_UP)   { /* handle C release */ }
}
```

### Rendering a Progress Bar or Stat
Use `draw_bar()` primitive or layer your own with `draw_fill()` and borders. Update values each tick.

### Using Sprites & Animations
Character sprites are managed via `sprite_play()`, `sprite_tick()`, `sprite_draw()`. Pass animation IDs (see character module) to queue animations.

## Libraries & Dependencies

Key external libraries (managed by PlatformIO):
- **TFT_eSPI** – ILI9488 display driver
- **SmartRC-CC1101-Driver-Lib** – Sub-GHz radio module
- **PN532** – NFC/RFID reader
- **IRremoteESP8266** – IR receiver/transmitter
- **OneWire / DallasTemperature** – Temperature sensor (optional)

All pinouts are in `config.h`; never bypass HAL to talk directly to peripherals.

## LobeHub Skills Marketplace

This repository has access to the **LobeHub Skills Marketplace**, a collection of over 100,000 specialized instruction sets that teach Copilot new capabilities. Use this to extend functionality beyond general coding.

### Available Skills

#### repo-surgeon
**For codebase audits, modernization, and production readiness checks**

Use when the user asks for:
- Full structural audit and dead code detection
- Folder restructuring or architecture redesign
- Hardcoded value extraction and configuration cleanup
- Naming standardization across the codebase
- Scalability risk analysis
- Rewrite of the worst/most problematic file
- Production-grade README generation

**Parameters** (pass as `mode=X scope=Y strictness=Z output=V`):
- `mode`: `report` (analyze only) | `patch` (include diffs) | `rewrite` (full rewrite)
- `scope`: `full` | `frontend` | `backend` | `api` | `shared` | `tests`
- `strictness`: `conservative` | `balanced` | `aggressive`
- `output`: `concise` | `standard` | `exhaustive`
- `risk_target`: e.g., `"10000 DAU"` (for scalability analysis)

**Core principles**:
- Never hallucinate facts — only based on actual repo code
- Cite evidence with file paths and line numbers
- Separate certainty levels: proven, high-confidence, needs verification
- Preserve behavior unless refactoring is explicitly requested
- Optimize for maintainability per unit of change

**Output includes**:
1. Executive summary with biggest problems/wins
2. Repository inventory (stack, architecture, entrypoints)
3. Dead code and deletion candidates with safety checks
4. Structural redesign recommendations with before/after trees
5. Hardcoded constant extraction map
6. Naming standardization rename map
7. Top 5 scalability risks grounded in actual code
8. Worst-file rewrite with full production-grade replacement
9. Generated or upgraded README.md
10. Action plan (30/60/90 minute phases)

---

### Finding More Skills

Search the marketplace for other capabilities:

```bash
npx -y @lobehub/market-cli skills search --q "KEYWORD"
```

Examples: `pdf`, `image editor`, `excel`, `deploy`, `email`, `database`, etc.

Install new skills:

```bash
npx -y @lobehub/market-cli skills install SKILL_IDENTIFIER
```

Read the installed `SKILL.md` in `.agents/skills/SKILL_IDENTIFIER/` to learn how to use it.

#### caveman
**Ultra-compressed communication mode for token efficiency**

Use when the user asks for:
- "caveman mode", "talk like caveman", "use caveman"
- "less tokens", "be brief", "save tokens"
- Efficiency-focused responses

**Reduces token usage ~75% while maintaining full technical accuracy** through terse, fragment-based communication.

**Intensity levels**:
- `lite` – Drop filler/hedging, keep articles + full sentences. Professional but tight.
- `full` (default) – Drop articles, fragments OK, short synonyms. Classic caveman mode.
- `ultra` – Abbreviate (DB/auth/config/req/res/fn), strip conjunctions, use arrows for causality (X → Y).
- `wenyan-lite` – Semi-classical Chinese. Drop filler but keep grammar, classical register.
- `wenyan-full` – Maximum classical terseness. Full 文言文 (80-90% character reduction).
- `wenyan-ultra` – Extreme abbreviation with classical Chinese feel.

**Activate**: `/caveman lite|full|ultra` or say "caveman mode"

**Deactivate**: "stop caveman" or "normal mode"

**Rules**:
- Drop: articles (a/an/the), filler (just/really/basically/actually/simply), pleasantries (sure/certainly/of course)
- Keep: technical terms exact, code blocks unchanged, error messages verbatim
- Use fragments. Use short synonyms (big not extensive, fix not implement).
- Pattern: `[thing] [action] [reason]. [next step].`

**Auto-Clarity**: Caveman pauses for security warnings, irreversible actions, multi-step sequences at risk of misread, and clarification requests. Resumes after clear part.

**Examples**:
- Bad: "Sure! I'd be happy to help you with that. The issue you're experiencing is likely caused by..."
- Good: "Bug in auth middleware. Token expiry check use `<` not `<=`. Fix:"

**Persistence**: Active every response. No revert after turns. Still active if unsure. Off only on explicit command.

---
