"""Export the player mesh (SKM_UEFN_Mannequin) to FBX for Blender.
Needs a renderer (commandlet mode asserts), so run the full editor:
RPG_QUIT_AFTER_EXPORT=1 UnrealEditor RPG_CODEX.uproject -ExecutePythonScript="<this file>"
"""
import os
import unreal

OUT_DIR = "C:/UNREAL PROJECTS/RPG_CODEX/Blender"
ASSET = "/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"

os.makedirs(OUT_DIR, exist_ok=True)
mesh = unreal.load_asset(ASSET)
if mesh is None:
    raise RuntimeError("Could not load " + ASSET)

options = unreal.FbxExportOption()
options.collision = False
options.level_of_detail = False
options.vertex_color = True

task = unreal.AssetExportTask()
task.object = mesh
task.filename = OUT_DIR + "/SKM_UEFN_Mannequin.fbx"
task.automated = True
task.prompt = False
task.replace_identical = True
task.options = options

ok = unreal.Exporter.run_asset_export_task(task)
unreal.log("RPG_EXPORT_RESULT ok=%s file=%s" % (ok, task.filename))
if os.environ.get("RPG_QUIT_AFTER_EXPORT"):
    unreal.SystemLibrary.quit_editor()
