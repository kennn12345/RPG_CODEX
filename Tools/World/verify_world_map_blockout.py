"""Read-only reload check of the saved map, every height sample, and collision."""
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
import json
import csv
import struct
import traceback
from pathlib import Path

root = Path(unreal.Paths.project_dir())
report_dir = Path(unreal.Paths.project_saved_dir()) / 'WorldMapBlockout'
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert les.load_level('/Game/RPG/Maps/L_WorldMap_Blockout')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ticks = 0

def verify(delta):
    global ticks
    ticks += 1
    if ticks < 120:
        return
    unreal.unregister_slate_post_tick_callback(handle)
    try:
        actors = eas.get_all_level_actors()
        land = [a for a in actors if isinstance(a,unreal.Landscape)]
        assert len(land)==1
        center, extent = land[0].get_actor_bounds(False)
        assert abs(extent.x*2-1400000)<2 and abs(extent.y*2-900000)<2
        assert abs(center.x)<1 and abs(center.y)<1
        unreal.SystemLibrary.execute_console_command(world,'RPG.Editor.VerifyWorldMapBlockout')
        native = json.loads((report_dir/'height_verification.json').read_text(encoding='utf-8-sig'))
        assert native['components']==640 and native['mismatches']==0, native
        markers = [a for a in actors if 'WorldMapSettlement' in [str(t) for t in a.tags]]
        assert len(markers)==19, ('settlement marker count', len(markers))
        by_name = {a.get_actor_label(): a for a in markers}
        with (root/'Tools/World/WorldMapSource/settlements.csv').open(newline='') as source:
            for row in csv.DictReader(source):
                loc = by_name[row['Name']+'_Location'].get_actor_location()
                assert abs(loc.x-float(row['UnrealX_cm']))<1 and abs(loc.y-float(row['UnrealY_cm']))<1, ('marker position',row['Name'],loc)
        water = next(a for a in actors if a.get_actor_label()=='Water_SeaLevel_0m_Blockout')
        water_loc = water.get_actor_location()
        assert max(abs(water_loc.x),abs(water_loc.y),abs(water_loc.z))<0.01, ('water location',water_loc)
        assert water.get_component_by_class(unreal.StaticMeshComponent).get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION, 'water collision'
        raw = (root/'Tools/World/WorldMapSource/heightmap.r16').read_bytes()
        probes = []
        # Exact source vertices avoid bilinear-vs-triangle interpolation differences.
        for px,py in ((50,50),(2000,1250),(3800,2300),(3000,300),(3400,2000),(1000,1000),(100,2300)):
            x,y = -700000+px*347.22222222222223, -450000+py*357.14285714285717
            expected = (struct.unpack_from('<H',raw,(py*4033+px)*2)[0]-32768)/128*100
            hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x,y,20000),unreal.Vector(x,y,-20000),
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE,True)
            values = hit.to_tuple()
            assert values[0], ('missing terrain collision',px,py)
            actual = values[4].z
            assert abs(actual-expected)<3, (px,py,expected,actual)
            probes.append(dict(vertex=[px,py],expected_cm=expected,hit_cm=actual))
        report = dict(reopened=True, extent_m=[extent.x/50,extent.y/50], center_cm=[center.x,center.y],
            height_verification=native, settlement_markers=len(markers),collision_probes=probes,passed=True)
        (report_dir/'reload_verification.json').write_text(json.dumps(report,indent=2))
        unreal.log('WORLD_MAP_RELOAD_VERIFIED '+json.dumps(report))
    except Exception as ex:
        unreal.log_error('WORLD_MAP_VERIFICATION_FAILED '+traceback.format_exc())
    unreal.SystemLibrary.quit_editor()

handle = unreal.register_slate_post_tick_callback(verify)
