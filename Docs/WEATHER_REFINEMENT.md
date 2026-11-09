# Weather and water refinement

Latest follow-up: see CLOUD_GROUND_FOG.md (November 8). Native module is 6864. It includes the isolated cloud-renderer fringe fix, denser terrain-hugging fog capped at about 20 ft, shorter/finer rain, half the prior stars and sun/moon sizes, and a darker night sky.

User request: fuller realistic clouds, softer lunar terminator, finer water-like rain, sunlight-dependent shallow-water effects, and fog that reaches the character while flying. Preserve the accepted moonless-night darkness. No seasons, commits, or pushes.

Implementation:

- Native weather now drives the existing engine volumetric-cloud material, with fuller density and weather-controlled coverage, albedo and wind. The sky shader suppresses its flat cloud overlay when the volumetric component is active (custom primitive data 16). The old fallback remains for maps without a volumetric-cloud component.
- The lunar terminator has a wider gradual grazing-light falloff and small terrain-relief modulation. Texture, moon size, phase calendar, and ambient night-light floor are retained.
- Rain uses 1,024 small, camera-facing, softly tapered additive quads instead of 512 long cubes. Their highlights follow the shared solar/lunar light amount, with no constant night brightness. This avoids opaque-looking thin-translucency artifacts. Rain is intentionally fine and subtle.
- Water foam has zero emission. Caustic intensity and underwater light rays use MPC_WeatherLighting.DirectSunlight, driven by solar elevation, cloud cover, and haze. It becomes zero at night. WaterLight scales specular response and the lake mist's emissive color/opacity with solar/lunar lighting. The bright moonless lake patch was the separate mist sheets. The accepted water volume coefficients and sky output remain intact. The mist asset contains disconnected duplicate custom nodes; the installer selects the node with connected inputs and reconnects both color and opacity explicitly.
- Fog weather removes height-fog and volumetric near-camera start/fade gaps, flattens the height falloff, and extends the volumetric range to 400 m. Original clear-weather fog settings are restored as the front clears.

Scripts: Tools/World/refine_weather_materials.py and updated install_moon_weather.py. The water rebuild recipe also retains the lighting changes. Before-state materials, exported shader source, material inspection, and an application report are under Saved/WeatherRefinement. No map asset was changed. Latest native module suffix: 6856; save/restart Unreal after completion.

Verification: native build 6856 passed. Calendar arithmetic, wait popup, lunar forecast, and runtime/panel tests all passed in a fresh instance. Runtime assertions include daytime/nighttime caustic lighting and near-camera fog settings. Eighteen fresh runtime renders cover six phases, six weather states, water at noon/new/full moon, fog while flying, nighttime rain, and an upward cloud view. Final review confirms fuller shaded clouds, a softened lunar edge, fine rain, continuous aerial fog, and dark moonless water without emissive mist or sheet outlines. Fresh-load material compilation is clean. Logs: Saved/Logs/WeatherRefinementRuntime.log and WeatherRefinementReady.log. Captures: Saved/MoonWeather/Review.
