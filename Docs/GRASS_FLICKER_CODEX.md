# Grass flicker: Codex follow-up

Date: 2026-11-05 (America/Chicago). Branch: asset-restyle. Implemented locally; user subsequently confirmed the flicker was fixed.

## Saved changes

- `M_Vista_Grass` and `M_Vista_GrassTall`: retain Two Sided Foliage, direct lighting and shadows, the original colour graph, soft normals, wind, LOD dithering, and distance fade. Material AO = 0 suppresses the noisy indirect-light contribution on these thin blades. A roughness-1 skylight cubemap sample in the up direction supplies stable ambient fill through Emissive, multiplied by the existing base colour and `StableSkyAmbientGain = 0.4`. The sampled sky already includes skylight colour/intensity and follows the existing day/night cycle.
- This is a deliberate stylized lighting tradeoff: these materials no longer receive local Lumen bounce colour or indirect self-occlusion. Sun/moon/local direct lights and their shadows still work. The tall-grass material is shared by some tufts, reeds and ferns, so those also receive this stabilization.
- `Config/DefaultEngine.ini`: `r.TSR.History.SampleCount=32`, `r.TSR.ShadingRejection.SampleCount=4` to reduce instability of moving blade edges. More temporal retention can soften moving detail or affect animated effects; review in normal gameplay.
- `Tools/World/build_vista_assets.py`: `stable_grass_ambient()` plus the `grass_lighting` builder. Full material rebuilds preserve the fix. The dedicated builder rebuilds ONLY the two grass materials, without importing meshes or rebuilding scatters/maps.
- No map, layout, sky/water assets, tree geometry, view-distance settings, or pre-existing user edits were changed. No push or commit was made.

## Validation and limits

Native lossless frame captures were taken from the running PIE world, with wind active, at the editor review view near (-587.6, 6825.17, 99.33), pitch -14.25, yaw -86.933. A fixed 60 Hz simulation prevented screenshot readback overhead from exaggerating wind movement. Daylight was held at 17.3 and night at 0 only in the temporary PIE world. Normal timing and the animated cycle were restored afterwards.

The final comparison used 48 frames per case. In the grass crop (x40..960, y340..650), after discarding the first eight frames, temporal changes above 6 Hz were measured with a detrended Hann-windowed FFT. Original daylight baseline/repeat RMS averaged 0.850 per pixel and 0.187 after 11-pixel spatial smoothing (8-bit grayscale units). The material + TSR candidate measured 0.595 and 0.107: approximately 30% / 43% less rapid variation. This metric separates some slow wind motion from rapid noise, but is NOT proof that all visible flicker is gone, nor a substitute for the user's review.

Mean crop brightness stayed close: daytime original 63.36/63.74 versus candidate 64.57; night original 31.31 versus material candidate 30.97. Wind and direct-light shadows remained visible. Saved graphs were checked: AO 0, custom Emissive input, Two Sided Foliage, two-sided true, world-space normal false tangent-space flag. Builder reported `ASSETS_DONE problems=0`; transient missing-input compilation warnings during graph construction were followed by completed compilation and a rendered PIE check.

Final saved-version camera-motion preview and timing: `C:/Users/cedar/Documents/Codex/2026-11-05/d/work/flicker_live/final/`. User-facing preview: `C:/Users/cedar/Documents/Codex/2026-11-05/d/outputs/Grass_Flicker_Fix_Preview.mp4`.

## Experiments deliberately not retained

Native/supersampled resolution, TAA substitution, ray normal-bias changes, grass ray visibility changes, Lumen gather-mode substitution, and much higher probe/ray budgets did not produce a sufficient consistent improvement. In the tested small editor viewport, baseline was about 8.7 ms, 16-ray-resolution configuration 11.3 ms, and denser 4-pixel probe grid 20.9 ms. These are editor-window samples, not packaged-game benchmarks.

The prior foliage history override of 100 is extremely permissive compared with this engine's constructor default 0.03 (queried via console help). Restoring a small threshold increased visible/metric noise in the existing lighting setup, so it was NOT saved as a standalone fix. Existing Lumen settings remain unchanged; the grass material now avoids that unstable contribution instead.

## Rebuild / rollback

With the editor closed, use the normal asset script with `grass_lighting` selected. For an already-open editor, execute in that SAME editor with `RPG_VISTA_ASSETS_KEEP_OPEN=1` and `sys.argv=[script_path,'grass_lighting']`; do not launch another editor and attempt to overwrite loaded packages. Never run `build_vista.py` for this fix.

To roll back, restore the two grass material assets and the corresponding source/config changes together. Before-change binary copies are in `C:/Users/cedar/Documents/Codex/2026-11-05/d/work/grass_original_assets/`. Scratch material copies were not assigned to the saved map; they were unloaded and removed from Content after testing. Temporary captures/scripts remain under the chat's `work/` directory.

Final saved-version check: 240 native frames (stationary, then a short translating/yawing camera move) were recorded and encoded to the preview above. Normal camera motion blur is present; this is not a quantitative ghosting certification. Without screenshot readback, the saved-version editor preview averaged 8.74 ms / 114 FPS at the tested 1353 x 753 viewport. This is not a full-screen or packaged-game result. The cycle was re-enabled, timing override restored to 0, and the player returned to the recorded BugItGo spot. The editor reported no dirty content packages after scratch cleanup.
