"""Put an Unreal-exported animation FBX (skeleton only) onto the player in RPG_Player.blend.
Run: blender -b Blender/RPG_Player.blend --python <this file> -- <anim.fbx> <out.blend>"""
import sys
import bpy

fbx, out = sys.argv[sys.argv.index("--") + 1:][:2]
player = bpy.data.objects["UEFN_Armature"]
before = set(bpy.data.objects)

# Same settings as import_player_blend.py so rest orientations match bone for bone.
bpy.ops.import_scene.fbx(filepath=fbx, global_scale=1.0, automatic_bone_orientation=False,
                         ignore_leaf_bones=True, use_anim=True)
imported = [o for o in bpy.data.objects if o not in before]
source = next(o for o in imported if o.type == 'ARMATURE')
action = source.animation_data.action
action.name = bpy.path.display_name_from_filepath(fbx)
action.use_fake_user = True

player.animation_data_create()
player.animation_data.action = action
if hasattr(player.animation_data, "action_slot") and action.slots:
    player.animation_data.action_slot = action.slots[0]
for o in imported:
    bpy.data.objects.remove(o, do_unlink=True)

scene = bpy.context.scene
start, end = (int(round(v)) for v in action.frame_range)
scene.frame_start, scene.frame_end = start, end
scene.frame_set(start)
bpy.ops.wm.save_as_mainfile(filepath=out)
posed = sum(1 for pb in player.pose.bones if pb.matrix_basis != pb.matrix_basis.Identity(4))
print("RPG_APPLY_RESULT action=%s frames=%d-%d fps=%s posed_bones=%d" % (
    action.name, start, end, scene.render.fps, posed))
