# Flight rain, occlusion and denser fog — November 8

Superseded by CLOUD_GROUND_FOG.md: module 6864, cloud-renderer silhouette fix, terrain-hugging 20 ft fog caps, half the stars and sky-disc sizes, darker background and shorter/finer rain. The wider fast-flight rain field and full lunar occlusion remain.

Latest module: 6859. User correction: the full moon must block stars even on its dark side; darken the sky rather than showing stars through it. Star count and disc sizes remain accepted. Rain needs wider coverage without catch-up delay, player-induced tilt, or pixel-box outlines on background silhouettes. Fog should be thicker still.

Implemented:

- Full lunar-disc occlusion restored. No shadow-side emission or halo on the shadowed surface. Foreground atmospheric haze remains in front of the moon; distant stars and galaxies are fully blocked. Night background is 82% darker; nebula/galaxy haze is 80% dimmer. Star distribution, brightness, size and twinkle are unchanged.
- Fog contribution .14 (was .085) and max opacity .96 during fog (restores original .8 in clear weather). Existing zero start-distance and flying-height fixes remain.
- Rain material responsive AA disabled: Unreal documents background aliasing around responsive small particles. Rain moved after DOF, with depth testing retained; no translucent velocity output. Finer, longer soft streaks with lower highlights and varied widths.
- 8,192 drops in one batch: 75% within an 18 m half-width, 25% within 55 m. Vertical field is -18 to +24 m. World positions fall only with wind/gravity. Every boundary wraps immediately, preserving drop height across horizontal travel. No top-only respawn gap or camera velocity added to rainfall. Near and boundary fading hide recycling. Separate roof/terrain probe grids cover both fields and refresh during fast travel.
- New math regression test covers rapid forward/reverse/climbing travel and large teleports, bounded coverage, preserved height and fall displacement. Motion captures now include 150 m/s forward/reverse flight with climbing, in addition to noon/dusk/full/new moon views.

Build 6859 and material application passed. All seven automated checks passed: calendar arithmetic, wait popup, forecast, fast-flight rain-field regression, runtime/panel, eighteen-view moon/weather review and six-stage rain motion review. Final halo correction passed a second fresh eighteen-view review with no material compilation failures. Visual review confirms star-free lunar shadows, darker sky, denser walking/flying fog, fine rain, cleaner background edges and populated rain during forward/reverse/climbing flight at 150 m/s. No user editor was running; command-line apply/test editors exited. Backups/report Saved/FlightRainRevision. Logs: Saved/Logs/FlightRainApply.log, FlightRainRuntime.log, FlightRainMaterialFinal.log, FlightRainReady.log. Capture directories Saved/MoonWeather/Review and Saved/RainRevision/Motion (six 40-frame stages). Nothing committed or pushed.
