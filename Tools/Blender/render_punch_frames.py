"""Render every frame of the open .blend's punch from the punching side and from the front (workbench, ortho).
Run: blender -b <file.blend> --python <this file> -- <r|l> <out_dir>
The character faces -Y in Blender; its right side is -X."""
import math
import os
import sys
import bpy
from mathutils import Vector

side, out = sys.argv[sys.argv.index("--") + 1:][:2]
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
target = Vector((0., -25., 92.))
x = -1. if side == 'r' else 1.
views = {"side": (Vector((x * 600., -25., 92.)), (math.radians(90), 0., math.radians(90 * x))),
         "front": (Vector((0., -600., 92.)), (math.radians(90), 0., 0.))}
for frame in range(scene.frame_start, scene.frame_end + 1):
    scene.frame_set(frame)
    for name, (location, rotation) in views.items():
        cam.location = location
        cam.rotation_euler = rotation
        scene.render.filepath = os.path.join(out, "%s_%03d.png" % (name, frame))
        bpy.ops.render.render(write_still=True)
print("RPG_RENDER_DONE", scene.frame_start, scene.frame_end)
