"""Build a volumetric-cloud height-profile texture in Unreal from a named recipe.

Run (commandlet): UnrealEditor-Cmd <uproject> -run=pythonscript -script="Tools/World/build_cloud_profile.py <recipe>"
Creates /Game/RPG/Environment/Weather/CloudProfiles/T_CloudProfile_<Recipe>.

The material samples the profile at UV = (height within the cloud layer 0 bottom..1 top,
cloud pattern strength 0 weak..1 strong); channel R/G/B is one cloud type each, mixed per
style by Layout_CloudType. Recipes start from the stock engine profile (T_Profile_08,
decoded in CloudProfileSource/stock_profile_08.f32) so untouched channels keep today's look.
"""
import sys, struct, zlib, math
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SRC = ROOT / 'Tools/World/CloudProfileSource/stock_profile_08.f32'
DEST = '/Game/RPG/Environment/Weather/CloudProfiles'


def load_stock():
    data = SRC.read_bytes()
    w, h = struct.unpack_from('<II', data, 0)
    px = struct.unpack_from('<%df' % (w * h * 4), data, 8)
    return w, h, [list(px[i:i + 4]) for i in range(0, len(px), 4)]


def smooth(a, b, x):
    t = min(1.0, max(0.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def tower(u, v):
    """Cumulus towers: flat base, strong pattern cores rise high, weak edges stop low."""
    top = 0.08 + 0.92 * smooth(0.2, 0.95, v)
    base = smooth(0.0, 0.03, u)
    cap = 1.0 - smooth(top - 0.1, top, u)
    return 0.06 + 0.56 * base * cap


def tower_soft(peak, fade, edge, rise_from=0.2, rise_to=0.95):
    """Like tower, but density thins with height so detail noise carves cauliflower tops.
    rise_from/rise_to: pattern strengths where clouds start growing tall / reach the layer top."""
    def fn(u, v):
        top = 0.08 + 0.92 * smooth(rise_from, rise_to, v)
        base = smooth(0.0, 0.03, u)
        cap = 1.0 - smooth(top - edge, top, u)
        return 0.06 + base * cap * (peak - fade * u)
    return fn


RECIPES = {
    'Stock': {},
    'Towers': {2: tower},
    'Towers2': {2: tower_soft(0.42, 0.12, 0.25)},
    'Towers3': {2: tower_soft(0.34, 0.08, 0.3)},
    'Towers4': {2: tower_soft(0.56, 0.16, 0.2)},
    'Towers5': {2: tower_soft(0.60, 0.22, 0.2)},
    'Towers6': {2: tower_soft(0.58, 0.18, 0.2, 0.05, 0.8)},
}


def build(recipe):
    w, h, px = load_stock()
    for ch, fn in RECIPES[recipe].items():
        for y in range(h):
            v = (y + .5) / h
            for x in range(w):
                px[y * w + x][ch] = fn((x + .5) / w, v)
    # 16-bit linear RGBA PNG, rows top-to-bottom.
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        for x in range(w):
            for c in range(4):
                raw += struct.pack('>H', int(round(min(1.0, max(0.0, px[y * w + x][c])) * 65535)))
    chunk = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 16, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b'')
    name = 'T_CloudProfile_' + recipe
    tmp = ROOT / 'Saved/CloudLook' / (name + '.png')
    tmp.parent.mkdir(parents=True, exist_ok=True)
    tmp.write_bytes(png)

    task = unreal.AssetImportTask()
    task.filename = str(tmp)
    task.destination_path = DEST
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex = unreal.load_asset(DEST + '/' + name)
    tex.set_editor_property('srgb', False)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_HDR)
    tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    assert unreal.EditorAssetLibrary.save_loaded_asset(tex, only_if_is_dirty=False)
    unreal.log('CLOUD_PROFILE_BUILT %s/%s' % (DEST, name))


for r in (sys.argv[1:] or ['Stock']):
    build(r)
