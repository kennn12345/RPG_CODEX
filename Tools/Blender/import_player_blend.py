"""Build RPG_Player.blend from the exported player FBX.
Run: blender -b --factory-startup --python <this file>"""
import bpy

FBX = "C:/UNREAL PROJECTS/RPG_CODEX/Blender/SKM_UEFN_Mannequin.fbx"
BLEND = "C:/UNREAL PROJECTS/RPG_CODEX/Blender/RPG_Player.blend"

bpy.ops.wm.read_factory_settings(use_empty=True)

# Centimetre scene so the 1.0-scale Unreal import reads at real size and round-trips unchanged.
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 0.01
scene.unit_settings.length_unit = 'CENTIMETERS'
scene.render.fps = 30

bpy.ops.import_scene.fbx(
    filepath=FBX,
    global_scale=1.0,
    automatic_bone_orientation=False,
    ignore_leaf_bones=True,
    use_anim=False,
)

arm = next(o for o in scene.objects if o.type == 'ARMATURE')
meshes = [o for o in scene.objects if o.type == 'MESH']
arm.name = "UEFN_Armature"
for m in meshes:
    m.name = "Player_Mesh"
arm.show_in_front = True
arm.data.display_type = 'STICK'

# Frame the viewport on the character when the file is opened.
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type == 'VIEW_3D':
            for space in area.spaces:
                if space.type == 'VIEW_3D':
                    space.clip_end = 100000.0

bpy.ops.wm.save_as_mainfile(filepath=BLEND)
height = max((o.dimensions.z for o in meshes), default=0.0)
print("RPG_BLEND_RESULT armature=%s bones=%d meshes=%s height_cm=%.1f" % (
    arm.name, len(arm.data.bones), [m.name for m in meshes], height))
