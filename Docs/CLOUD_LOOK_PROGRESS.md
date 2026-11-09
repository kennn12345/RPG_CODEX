# Cloud look — realistic restyle progress

November 9, 2026. Goal from the user: **realistic clouds for now**, judged against real cloud photos, tweak later. Work one change at a time; each is shown before/after next to real photos and approved before the next.

## Approved and in code (`Source/RPG_CODEX/RPGSkyCycle.cpp`, cloud block in `Apply()`)

| Style | Change | Old → new |
|---|---|---|
| Puffs (Scattered) | More clouds | coverage −0.30 → −0.10 → **0.00** (with the thinner layer) |
| Puffs | Denser, crisper | density 0.05 → **0.30** |
| Puffs | Flatter bases | layer thickness 3 km → **2 km** |
| All styles | Shadows neutral grey, not beige | cloud base colour 0.98/0.99/1.0 → **0.68/0.80/1.0** (sunlit tops stay white; sun colour untouched) |
| Tall billows | Bigger, denser, separated | density 0.065 → **0.30**, coverage −0.22 → **−0.05**, pattern scale 55 → **90** |
| Overcast | Full grey ceiling | coverage 0.8 → **1.5**, density 0.07 → **0.20**, pattern scale 30 → **60** |

Overcast is also the Rain preset's cloud style, so rain skies changed with it.

## Tried and rejected

- Puffs: stronger noise (wispy/ragged), more view samples (no visible change).
- Shadows: weaker sun cloud-scatter, sky-light bottom occlusion, less multiscatter — all kept the brown; colour on the clouds fixed it.
- Billows custom tower profile (`T_CloudProfile_Towers6`, blue channel): real towers but background clouds read as smoke plumes; user preferred the stock shape. **Not in the game.**
- Overcast texture: stronger noise, smaller pattern, denser deck — all still smooth.

## Where we paused

Choice pending: **try the fine-detail noise switch** (`UseNoise3` static switch on the cloud material — needs a separate material copy, affects all styles, some GPU cost) or **move on to Storm deck** (currently flat grey haze; storm shading in the material is forced off via `StormClouds=0`). Broken has had no style-specific edit yet beyond density/colour inherited changes.

## How to review

- Capture: `Tools/World/capture_cloud_look.ps1 -Tag Name [-Styles 1-6 digits] [-Scalar "A=1;B=2"] [-Vector "C=r,g,b,a"] [-Texture "Param=/Game/path.Asset"]` runs automation test `Visual.CloudStylesNoon` (`Source/RPG_CODEX/RPGCloudReviewTest.cpp`) in a hidden editor at noon from the Overlook, two views per style, output `Saved/CloudLook/<Tag>`. The user's open editor is untouched. Overrides go through `ARPGSkyCycle::Cloud*Overrides` (empty in normal play). Extra scalar keys: `LayerBottomKm`, `LayerHeightKm`, `ViewSampleScale`, `SkyBottomOcclusion`, `SunCloudScatter`.
- View: `.claude/launch.json` config `cloud-look` (node `Tools/World/serve_cloud_look.js`, port 8765). Pages in `Tools/World/CloudLookPages`: `compare.html` (all styles vs real photos, expects `Saved/CloudLook/Noon`), `review.html?style=2_puffs&before=A&after=B&refs=File1.jpg|File2.jpg`, `review_all.html?before=A&after=B`. Real photos are hot-linked from Wikimedia Commons.
- Use `FScreenshotRequest`, not `HighResShot` — HighResShot captures the editor viewport during PIE.

## Cloud material facts (from `Tools/World/dump_cloud_graph.py`)

The height profile (`Layout_CloudHeightProfile`) is sampled at UV = (height within layer, cloud-pattern strength); channels R/G/B are three cloud types mixed per style by `Layout_CloudType`. The stock `T_Profile_08` is a soft, low-contrast gradient, so stronger pattern does not rise higher — why stock clouds never tower. Profile values below ~0.4 barely form cloud. `Tools/World/build_cloud_profile.py <Recipe>` builds profiles in Unreal from `CloudProfileSource/stock_profile_08.f32` (stock data decoded once via Blender); `T_CloudProfile_Stock` is a verified exact copy (<1/255 difference in all views).
