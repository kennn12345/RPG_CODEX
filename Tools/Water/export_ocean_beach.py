"""Blender: exports a dense ocean surface tile (for wave displacement) and a beach test terrain for L_WaterTest.
blender -b --python export_ocean_beach.py   (the FBX importer mirrors Blender Y: land at Blender -Y ends up at Unreal +Y)"""
import bpy, bmesh, math
OUT = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Water_R01/exports/unreal'
bpy.ops.wm.read_factory_settings(use_empty=True)

def grid(name, size_x, size_y, nx, ny, zf, uv_tile=5.0):
    bm = bmesh.new(); vs = []
    for j in range(ny + 1):
        row = []
        for i in range(nx + 1):
            x = -size_x / 2 + size_x * i / nx; y = -size_y / 2 + size_y * j / ny
            row.append(bm.verts.new((x, y, zf(x, y))))
        vs.append(row)
    for j in range(ny):
        for i in range(nx): bm.faces.new((vs[j][i], vs[j][i + 1], vs[j + 1][i + 1], vs[j + 1][i]))
    uv = bm.loops.layers.uv.new('UVMap')
    for f in bm.faces:
        for l in f.loops: l[uv].uv = (l.vert.co.x / uv_tile, l.vert.co.y / uv_tile)
    me = bpy.data.meshes.new(name); bm.to_mesh(me); bm.free()
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob); return ob

def vn(x, y, s): return math.sin(x * 1.3 * s + math.sin(y * .9 * s) * 1.7) * math.cos(y * 1.1 * s - math.sin(x * .7 * s) * 1.3)

def beach_z(x, y):
    # land toward -Y (dunes ~ +2.5 m), beach slope through the waterline near y = -18, sand bar, deep sea toward +Y
    t = (y + 18.0) / 70.0
    z = -6.5 * max(0.0, min(1.0, t)) ** 0.8 if t > 0 else min(2.6, -t * 70.0 * 0.09)
    z += 0.35 * math.exp(-((y - 6.0) / 4.0) ** 2)                              # sand bar
    z += 0.25 * vn(x, y, .25) + (0.5 * vn(x, y, .12) if y < -22 else 0.0)       # ripples / dunes
    return z

for ob, fn in ((grid('SM_Water_OceanTile', 160, 160, 200, 200, lambda x, y: 0.0), 'SM_Water_OceanTile'),
               (grid('SM_TestTerrain_Beach', 160, 140, 320, 280, beach_z, 2.0), 'SM_TestTerrain_Beach')):
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{fn}.fbx', use_selection=True, axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE')
    print('EXPORTED', fn, len(ob.data.polygons))
