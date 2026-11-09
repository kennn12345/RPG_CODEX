# Night sky detail pass

## Fewer small stars

The two smallest star layers now retain roughly half their previous population,
using a second stable random test per star. Retained stars keep their positions,
sizes, brightness and twinkle. The two larger layers and the accepted softened
galaxy shading are unchanged. Applied with `thin_small_night_stars.py`; the
before-state material, source and report are in `Saved/NightSkyFewerSmallStars`.

## Softer galaxy shading, November 6 evening

Following the user's star-dominant reference image, reduced broad nebula emission
from 0.038 to 0.012 and concentrated it toward the irregular galactic band. The
bright filament layer is 28% of its previous strength; Milky Way emission is
0.032 instead of 0.052, and low horizon glow is halved. This leaves clearer dark
sky and reduces the murky web appearance. Star scatter, brightness and twinkle
are identical, as are the small galaxies, moon, sun, daytime and calendar pace.

`soften_night_galaxy.py` applies this night-only change to the live material.
Backups and verified before/after source are in `Saved/NightSkySoftened`. The
current `night_sky_detail.hlsl` is the rebuild source for this softer version.

## Ring correction, November 6 evening

The user's gameplay video exposed curved rows of stars near the zenith. The old
star shader intersected a regular 3D point grid with the sky direction sphere;
successive grid slices produced those visible arcs. `night_sky_detail.hlsl` now
scatters independently jittered points on six projected faces, with separate fixed
rotations per layer, area-corrected density, and overlap near face boundaries.
Star locations stay fixed; the existing gentle brightness animation remains.

Nebula coverage now uses irregular noise masks instead of four round directional
lobes. The Milky Way's path and width vary more, breaking up its regular ribbon.
`randomize_night_sky.py` applied the night-only replacement in the open editor.
Before/after source and an asset backup are under `Saved/NightSkyRandomized`.
The daytime prefix, sun/moon suffix, and material scalar values were preserved.
Fresh-load overhead, moon, horizon, and noon captures were checked, and material
compilation logs contained no failures. No level was saved and no commit was made.

The material remains shared by maps using `M_Vista_Sky`; the update is already
loaded in the live editor and does not need a restart.

## Earlier detail pass

November 6, 2026. Updated the night block of `M_Vista_Sky` in the user's live
L_WorldVista session. No map was saved, and the existing editor was left open.

The Milky Way is now a connected, irregular blue-violet ribbon with finer cloudlets,
dark dust lanes and denser star fields along it. Nebulae have more wispy structure
and less orange haze. The two spiral galaxies have more defined arms and mottling;
the main galaxy is positioned beside the moon in the overhead review view.

Stars retain fixed positions and vary brightness with two independent, slow sine
rhythms per star. Larger stars have a little more variation; small stars remain
restrained. The material Time expression is connected to T and ignores pause.
Real-time viewport rendering or gameplay is needed to see time advance.

Source: `Tools/World/night_sky_detail.hlsl`, loaded by `set_night_sky.py`.
`apply_night_sky_detail.py` performs the night-only replacement without closing
the live editor. All scalar parameters were preserved, including SunSize 6.4° and
MoonSize 8° radii. The custom-node prefix and sun/moon suffix were checked against
the previous shader and are unchanged by this pass.

Before-state asset backup, before/after shader text and verification reports are in
`Saved/NightSkyDetail`. Material compilation logs were checked with no failures.
Fresh-load sky and ground renders were inspected. The procedural sky's final frame
cost has not been profiled.

Automation screenshots barely advanced material time, so the animated preview was
rendered from an unsaved material copy at controlled 0.125-second shader-time steps.
The saved/live material continues to use real material Time. Across 64 controlled
frames, 128 sampled bright stars had a median brightness range of about 20%; the
moon stayed stable. Preview video was encoded and fully decoded successfully.
No preview material was intentionally saved to Content.

To review in Unreal, select SkyCycle and set Time Of Day to 0. The daytime sky,
moon appearance and approved smaller sun/moon discs are preserved. Nothing was
committed or pushed.
