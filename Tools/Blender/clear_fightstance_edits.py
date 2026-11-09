"""Remove all hand-made bone edits (Additive Layer Tracks) from RPG_FightStance in the open editor.
Run from the editor Cmd box: py "C:/UNREAL PROJECTS/RPG_CODEX/Tools/Blender/clear_fightstance_edits.py"
Undoable (Ctrl+Z); does not save the asset."""
import unreal

ASSET = "/Game/RPG/Animations/FightStance/RPG_FightStance"

seq = unreal.load_asset(ASSET)
model = seq.get_editor_property("data_model")
names = [c.get_editor_property("curve_id").get_editor_property("curve_name")
         for c in model.get_transform_curves()] if hasattr(model, "get_transform_curves") else []
with unreal.ScopedEditorTransaction("Clear FightStance additive layer tracks"):
    seq.modify()
    seq.get_editor_property("controller").remove_all_curves_of_type(unreal.RawCurveTrackTypes.RCT_TRANSFORM)
unreal.log("RPG_CLEAR_RESULT removed=%d %s" % (len(names), [str(n) for n in names]))
