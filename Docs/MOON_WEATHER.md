# Moon phases and weather

Implemented for maps using `ARPGSkyCycle`, including L_WorldVista. No seasons. Nothing was committed or pushed.

## In-game testing

Save work and restart Unreal to load the native module. Open L_WorldVista, Play, click the game view, then press **F6**. The Sky & weather panel provides clear/haze/cloudy/fog/rain/storm presets, automatic mode, optional eight-second test blends, phase/date previews, previous/next day, midnight/noon, manual lightning, and independent rain/lightning/audio switches. F6/Esc/controller B closes it. Mouse, Tab/Enter, and controller navigation are supported.

The panel leaves the clock and world running while blocking player movement/look. NPC on-screen debug labels and the gameplay reticle are suppressed while testing and restored on close. Regular D-Pad Down waiting retains its paused 1–24-hour confirmation window.

Moon preview buttons change the current month's calendar day and select a viewing hour. Full = day 30 at midnight; waning half = day 8 before dawn; new = day 15 around noon; waxing half = day 23 at night; crescents use days 12/19 near dawn/dusk. Look around/up, since phase changes also change moonrise/set. A new moon is almost invisible.

## Calendar and lunar rendering

The existing 120-real-minute full day remains unchanged. The lunar cycle is exactly 30 game days, with full moon at the start of day 30 and new moon at the start of day 15. It continues smoothly across months and years. Waiting advances phase and forecast immediately. The moon's illuminated edge is computed from sunlight in its local frame, without rotating a mesh or changing its approved texture/size. Moonlight strength follows illuminated area, with a minimum ambient floor for playability. Haze softens/warms the disc and halo; small seeded daily warmth variations blend continuously.

## Weather

`URPGWeatherSubsystem` computes a repeatable nonseasonal forecast from elapsed game seconds and WeatherSeed (default 7319). Fronts last six game hours and develop over the first game hour. Probabilities: clear 32%, haze 15%, cloudy 20%, fog 10%, rain 16%, storm 7%. These are editable code defaults, unrelated to months or seasons. Manual overrides blend from the current state and are cleared with Resume automatic weather. Clock rewinds used by the test panel cannot strand a transition in the future.

Weather affects sun/moon transmission, ambient lighting, exposure, fog density/falloff/start distance, and the existing volumetric-cloud actor's coverage, density, color and wind. The sky shader disables its flat cloud overlay when volumetric clouds are available. Fog reaches the camera, including while flying. No map asset was modified. See WEATHER_REFINEMENT.md for the water and rendering refinements.

`URPGWeatherEffectsComponent` uses one bounded batch of 8,192 fine, tapered, camera-facing rain quads simulated in world space around the local camera, with soft additive highlights driven by the dedicated day/moon/lightning rain lighting amount. Two grids of twenty-five local ground/roof probes clip rain against terrain and shelter. Rain audio is quieter under shelter. Lightning flashes light the sky/world and trigger thunder after a 1.8–3.8-second delay; automatic storm flashes are spaced 12–30 real seconds apart. Rain and thunder PCM are synthesized in C++ and need no imported recordings. This is a local visual/audio weather implementation; rain does not yet create puddles, surface wetness, water accumulation, or gameplay effects.

The calendar and forecast survive map travel through game-instance subsystems. Calendar state, overrides, and weather are not automatically saved to disk across game launches. The existing calendar save-state hooks remain available.

## Assets and rebuilding

`Tools/World/install_moon_weather.py` installs custom primitive data inputs 8–16 and the lunar/weather suffix of the shared M_Vista_Sky. It preserves the accepted stars, reduced tiny-star population, galaxy shading, daytime prefix, sun code, and disc-size scalars. It creates M_Weather_Rain under /Game/RPG/Environment/Weather; that directory is explicitly cooked. Run material setup in the open editor if another editor has the shared sky loaded: a separate process cannot save a locked package. The original material/source backup and installation report are in Saved/MoonWeather. `set_night_sky.py` preserves the phased moon when rebuilding an already installed weather sky.

Latest native module: suffix 6864. The user's editor session was preserved. Save/restart it to load the refinements and F6 binding.

## Verification

Native editor build passed. Tests passed for calendar arithmetic and waiting, lunar dates/forecast continuity, weather transitions and clock skips, F6 action/panel open and close, rain audio, delayed thunder, effect/audio toggles, and first/third-person stance input regression. Twelve runtime renders covered full/waning/crescent/new/waxing moons plus all six weather types. Fresh-load material logs had no compilation failures.

Logs: Saved/Logs/MoonWeatherFinal.log (seven passing tests), MoonWeatherVerified.log (five passing tests after sunlight-oriented phase shading), MoonWeatherReady.log (final panel and wait checks). Captures: Saved/MoonWeather/Review and TestPanel.png. 
