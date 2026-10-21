# RPG_CODEX — Xbox Controller Scheme (authoritative)

Last updated: 2026-10-20. This file is the single source of truth for controls.
If anything else (old summaries, chat prompts, comments) disagrees with this file, this file wins.
Implemented in `Source/RPG_CODEX/RPGControllerInputComponent.cpp`.

## Philosophy

- Simple controls. The same button keeps the same broad purpose in every state.
- **The buttons don't change, context changes the animation** (sprint / jump / flight versions of attacks).
- **Where the player looks is where they fight.** The Right Stick aims; it never selects combos.
- No combo strings, directional combos or skill-specific button sequences.

## Buttons

| Button | Purpose |
|---|---|
| **RT** | Right-side physical attack. Tap = normal, hold = charge, release = heavy/power by charge. Full charge holds until release (never auto-fires). Shield: RT while blocking = shield bash. Bow: fire. |
| **LT** | Contextual left side. Unarmed / dual wield: left attack (same tap/hold/charge). 1H with empty off-hand, 2H, shield: block. Bow: aim. |
| **RT + LT** | Unarmed: fighting guard. Dual wield: dual guard. |
| **RB / LB** | Right / left spell. Defensive spells also go here (never LT). Spell charging is per-spell, separate from melee charge. |
| **Y** | Kick. Tap = normal, hold = charge (leg locked at charge start), legs alternate. Sprint / jump / flight change the animation. |
| **B** | Dodge in Left Stick direction. Flying: aerial evade. |
| **X** | Use equipped quick consumable. |
| **A** | Ground: tap = jump, hold = charged (higher) jump. Interaction takes priority when a valid interactable is targeted. In the air: start flight. Flying: hold = rise straight up. |
| **Left Stick** | Move. |
| **L3** | Sprint. Flying: toggle fast flight. |
| **Right Stick** | Look / aim. |
| **R3** | Ground: crouch / stealth toggle. Flying: **hold (0.25 s)** to stop flying and fall. |
| **D-Pad Up** | Hotkey / quick-select. |
| **D-Pad Left** | Unequip / put away weapons. |
| **D-Pad Right** | Tap: first ↔ third person. Hold: third-person camera distance (adjust with stick). |
| **D-Pad Down** | Wait / sleep. |
| **View** | Journal / quest log. |
| **Menu** | Main menu (inventory, stats, map, spells, hotkeys, save/load, settings). |

## Flight

| Input | Effect |
|---|---|
| A while in the air (after a jump **or** falling off a ledge) | Start flying (hover in place) |
| Left Stick + look up / down | Fly where you look: forward climbs when looking up, dives when looking down. Strafing stays level. Small camera tilts (under 8°) keep level flight. |
| Hold A | Rise straight up |
| L3 | Toggle fast flight |
| Hold R3 (0.25 s) | Stop flying and fall |
| B | Aerial evade |

All combat buttons keep their ground meaning while flying (RT/LT attacks, RB/LB spells, Y kick).
Tunables: `RPGMovementAbilityComponent` (`FlightPitchDeadZone`, `FlightMaxPitch`, `FastFlightSpeed`,
`bFlightFollowsLookPitch`) and `RPGControllerInputComponent` (`FlightExitHoldTime`).

## Context = animation, not new buttons

| State | RT | Y |
|---|---|---|
| Normal | right attack | kick |
| Sprinting | sprint / blitz attack | sprint kick |
| Jumping | jumping attack | jumping kick |
| Flying | flying attack | flying kick |

## Open design questions

- None for flight currently. Keyboard/mouse bindings for RPG actions don't exist yet (controller only).
