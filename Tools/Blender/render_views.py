"""Render front / side / three-quarter workbench previews of the open .blend.
Run: blender -b <file.blend> --python <this file> -- <output_prefix>"""
import math
import sys
import bpy
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
prefix = args[0]
zoom = len(args) > 1 and args[1] == "hands"  # close-up on the guard
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.render.resolution_x, scene.render.resolution_y = 520, 760
scene.display.shading.light = 'STUDIO'
scene.display.shading.show_cavity = True

cam = bpy.data.objects.new("PreviewCam", bpy.data.cameras.new("PreviewCam"))
scene.collection.objects.link(cam)
cam.data.type = 'ORTHO'
cam.data.ortho_scale = 60.0 if zoom else 200.0
cam.data.clip_end = 10000.0
scene.camera = cam
target = Vector((0, -15, 130)) if zoom else Vector((0, 0, 88))

for name, yaw in (("front", 0), ("side", 90), ("threequarter", 40)):
    a = math.radians(yaw)
    cam.location = target + Vector((math.sin(a) * 500, -math.cos(a) * 500, 0))
    cam.rotation_euler = (math.radians(90), 0, a)
    scene.render.filepath = "%s_%s.png" % (prefix, name)
    bpy.ops.render.render(write_still=True)
