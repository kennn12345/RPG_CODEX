"""Blender: exports the 10 m river strip turned to lie along Unreal's +X (the FBX importer mirrors Blender Y), which is
the forward axis ARPGStylizedRiver bends. blender -b <Water_Kit_R01.blend> --python export_river_strip_x.py"""
import bpy, math
from mathutils import Matrix
src = bpy.data.objects['SM_Water_River_Straight10']
ob = src.copy(); ob.data = src.data.copy(); ob.name = 'SM_Water_River_Strip10'; bpy.context.scene.collection.objects.link(ob)
ob.location = (0, 0, 0); ob.data.transform(Matrix.Rotation(math.radians(-90), 4, 'Z')); ob.data.update()   # +Y -> +X
bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.fbx(filepath='C:/Users/cedar/Documents/Codex/2026-11-02/ok/outputs/Water_R01/exports/unreal/SM_Water_River_Strip10.fbx',
                         use_selection=True, axis_forward='-Y', axis_up='Z', mesh_smooth_type='FACE', colors_type='LINEAR')
print('STRIP', tuple(round(v, 2) for v in ob.dimensions))
