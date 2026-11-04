"""Blender: inspect the AI-permitted Fab tree packs (CC BY 4.0) and render a lineup preview.
  blender -b --disable-autoexec --python inspect_fab_trees.py -- <lux|jabami>
Lux Art Studios "Vegetation Stylized Kit" (Trees.blend, FilesPlants.blend) and JABAMI Production "Jabami Anime Tree v4"
(medium01.fbx). Prints INSPECT lines (objects, triangles, size, materials, images) and writes a lineup PNG."""
import bpy, sys, math, os
from mathutils import Vector
PACKS = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/FabPacks'
which = (sys.argv[sys.argv.index('--') + 1:] or ['lux'])[0]
if which == 'lux':
    bpy.ops.wm.open_mainfile(filepath=f'{PACKS}/Lux_VegetationStylizedKit/extracted/Trees.blend', load_ui=False)
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=f'{PACKS}/Jabami_AnimeTree_v4/extracted/source/inner/medium01.fbx')
meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
for o in meshes:
    tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
    mats = [m.name if m else None for m in o.data.materials]
    print(f'INSPECT {o.name} parent={o.parent.name if o.parent else None} tris={tris} dims={tuple(round(d, 2) for d in o.dimensions)} loc={tuple(round(v, 1) for v in o.location)} mats={mats}')
for img in bpy.data.images:
    print(f'INSPECT_IMG {img.name} {img.size[0]}x{img.size[1]} packed={bool(img.packed_file)} path={img.filepath}')
for m in bpy.data.materials:
    if not m.use_nodes: continue
    kinds = sorted({n.type for n in m.node_tree.nodes})
    print(f'INSPECT_MAT {m.name} blend={getattr(m, "blend_method", "?")} nodes={kinds}')
# lineup render: every top-level mesh group side by side, golden-hour light
sc = bpy.context.scene; sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
tops = [o for o in meshes if o.parent is None or o.parent.type != 'MESH']
x = 0.0
for o in sorted(tops, key=lambda o: o.name):
    w = max(o.dimensions.x, o.dimensions.y, 1.0)
    o.location = Vector((x + w / 2, 0, 0)); x += w * 1.2
w_ = bpy.data.worlds.new('W'); sc.world = w_; w_.use_nodes = True; w_.node_tree.nodes['Background'].inputs[0].default_value = (.45, .6, .9, 1)
sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN')); sc.collection.objects.link(sun); sun.data.energy = 4
sun.rotation_euler = Vector((-.5, .6, -.6)).normalized().to_track_quat('-Z', 'Y').to_euler()
cam = bpy.data.objects.new('C', bpy.data.cameras.new('C')); sc.collection.objects.link(cam); sc.camera = cam
h = max((o.dimensions.z for o in tops), default=5)
d = Vector((0, -1, .18)).normalized(); cam.data.lens = 35
cam.location = Vector((x / 2, 0, h * .45)) + d * max(x * .75, h * 2); cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
sc.render.resolution_x, sc.render.resolution_y = 1600, 600
sc.render.filepath = f'{PACKS}/{"Lux_VegetationStylizedKit" if which == "lux" else "Jabami_AnimeTree_v4"}/lineup.png'
bpy.ops.render.render(write_still=True)
print('INSPECT_DONE', len(meshes))
