"""Render every frame of the open .blend's punch from several angles (workbench, ortho).
Run: blender -b <file.blend> --python <this file> -- <r|l> <out_dir> [all]
Default views: the punching side and the front. With "all": front, front three-quarter, punching side, back
three-quarter, back and the far side (camera yaw relative to the front, toward the punching side first).
The character faces -Y in Blender; its right side is -X."""
import math
import os
import sys
import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
side, out = args[:2]
everything = len(args) > 2 and args[2] == "all"
os.makedirs(out, exist_ok=True)
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x, scene.render.resolution_y = 480, 540
scene.display.shading.light = 'STUDIO'
scene.display.shading.show_cavity = True
scene.render.film_transparent = False

cam = bpy.data.objects.new("ReviewCam", bpy.data.cameras.new("ReviewCam"))
scene.collection.objects.link(cam)
cam.data.type = 'ORTHO'
cam.data.ortho_scale = 215.0
cam.data.clip_end = 10000.0
scene.camera = cam
toward = -1. if side == 'r' else 1.   # punching side: -X for the right hand
yaws = ({"front": 0., "front34": 45., "side": 90., "back34": 135., "back": 180., "farside": -90.} if everything
        else {"side": 90., "front": 0.})
center = Vector((0., -25., 92.))
for frame in range(scene.frame_start, scene.frame_end + 1):
    scene.frame_set(frame)
    for name, yaw in yaws.items():
        a = math.radians(yaw * toward)   # camera position angle; toward the punching side for positive yaws
        cam.location = center + Vector((600. * math.sin(a), -600. * math.cos(a), 0.))
        cam.rotation_euler = (math.radians(90), 0., a)
        scene.render.filepath = os.path.join(out, "%s_%03d.png" % (name, frame))
        bpy.ops.render.render(write_still=True)
print("RPG_RENDER_DONE", scene.frame_start, scene.frame_end, list(yaws))
