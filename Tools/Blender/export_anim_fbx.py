"""Export an AnimSequence (skeleton motion only) to FBX for review in Blender.
RPG_EXPORT_ANIM=/Game/... UnrealEditor-Cmd RPG_CODEX.uproject -run=pythonscript -script=<this file>"""
import os
import unreal

OUT_DIR = "C:/UNREAL PROJECTS/RPG_CODEX/Blender"
ASSET = os.environ["RPG_EXPORT_ANIM"]

seq = unreal.load_asset(ASSET)
if seq is None:
    raise RuntimeError("Could not load " + ASSET)

options = unreal.FbxExportOption()
options.collision = False
options.level_of_detail = False
options.export_preview_mesh = False  # Commandlet has no renderer for the mesh; Blender supplies it.
options.map_skeletal_motion_to_root = False

task = unreal.AssetExportTask()
task.object = seq
task.filename = "%s/%s.fbx" % (OUT_DIR, seq.get_name())
task.automated = True
task.prompt = False
task.replace_identical = True
task.options = options
task.exporter = unreal.AnimSequenceExporterFBX()

ok = unreal.Exporter.run_asset_export_task(task)
unreal.log("RPG_EXPORT_RESULT ok=%s file=%s" % (ok, task.filename))
