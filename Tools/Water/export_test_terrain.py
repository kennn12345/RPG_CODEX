"""Blender: exports the water kit's preview beds as test-map terrain for Unreal (not game assets).
blender -b <Water_Kit_R01.blend> --python export_test_terrain.py"""
import bpy
OUT = 'C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Water_R01/exports/test_scene'
for name, fn in (('_Bed_River', 'SM_TestTerrain_River'), ('_Bed_Lake', 'SM_TestTerrain_Lake')):
    ob = bpy.data.objects[name]; loc = ob.location.copy(); ob.location = (0, 0, 0)
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    if not ob.data.uv_layers:
        ob.data.uv_layers.new(name='UVMap')
        for l in ob.data.loops:
            co = ob.data.vertices[l.vertex_index].co; ob.data.uv_layers[0].data[l.index].uv = (co.x / 2, co.y / 2)
    bpy.ops.export_scene.fbx(filepath=f'{OUT}/{fn}.fbx', use_selection=True, axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE')
    ob.location = loc; print('EXPORTED', fn, tuple(round(v, 2) for v in ob.dimensions))
