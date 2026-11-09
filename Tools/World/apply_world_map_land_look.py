"""Assign a dedicated region material to the refined map only, then capture it."""
import unreal,json,shutil,traceback,os
from pathlib import Path
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
ROOT=Path(unreal.Paths.project_dir())
SOURCE=ROOT/'Tools/World/WorldMapLookSource'
REPORT=Path(unreal.Paths.project_saved_dir())/'WorldMapLandLook';REPORT.mkdir(exist_ok=True,parents=True)
ASSETS='/Game/RPG/Environment/WorldMap/LandLook'
MAP='/Game/RPG/Maps/L_WorldMap_Refined'
eal=unreal.EditorAssetLibrary
mel=unreal.MaterialEditingLibrary
eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
ues=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
shutil.copy2(ROOT/'Content/RPG/Maps/L_WorldMap_Refined.umap',REPORT/'L_WorldMap_Refined_before_land_look.umap')
assert les.load_level(MAP)
world=ues.get_editor_world()
textures={}
for name,kind in [('T_WorldMap_RegionColor','color'),('T_WorldMap_GroundGrain','linear'),('T_WorldMap_GroundNormal','normal')]:
    path=ASSETS+'/'+name
    assert not eal.does_asset_exist(path), 'Land look assets already exist; refusing accidental replacement'
    task=unreal.AssetImportTask();task.filename=str(SOURCE/(name+'.png'));task.destination_path=ASSETS;task.destination_name=name
    task.automated=True;task.save=False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex=unreal.load_asset(path);assert tex,path
    tex.set_editor_property('srgb',kind=='color')
    if kind=='normal':tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
    if kind=='color':
        tex.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP)
        tex.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
    assert eal.save_loaded_asset(tex)
    textures[name]=tex
name='M_WorldMap_RegionalTerrain'
assert not eal.does_asset_exist(ASSETS+'/'+name)
mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,ASSETS,unreal.Material,unreal.MaterialFactoryNew())
def node(cls,x,y,**props):
    n=mel.create_material_expression(mat,cls,x,y)
    for k,v in props.items():n.set_editor_property(k,v)
    return n
def custom(code,names,kind,x,y):
    n=node(unreal.MaterialExpressionCustom,x,y,code=code,output_type=kind)
    inputs=[]
    for name in names:
        ci=unreal.CustomInput();ci.set_editor_property('input_name',name);inputs.append(ci)
    n.set_editor_property('inputs',inputs);return n
def link(a,ao,b,bi):assert mel.connect_material_expressions(a,ao,b,bi)
wp=node(unreal.MaterialExpressionWorldPosition,-1200,0)
vn=node(unreal.MaterialExpressionVertexNormalWS,-1200,180)
uv=custom('return saturate((WP.xy + float2(700000,450000)) / float2(1400000,900000));',['WP'],unreal.CustomMaterialOutputType.CMOT_FLOAT2,-950,-300)
link(wp,'',uv,'WP')
tile=custom('return WP.xy / 800.0;',['WP'],unreal.CustomMaterialOutputType.CMOT_FLOAT2,-950,400)
link(wp,'',tile,'WP')
region=node(unreal.MaterialExpressionTextureSample,-650,-350,texture=textures['T_WorldMap_RegionColor'])
link(uv,'',region,'UVs')
grain=node(unreal.MaterialExpressionTextureSample,-650,150,texture=textures['T_WorldMap_GroundGrain'],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
link(tile,'',grain,'UVs')
normal=node(unreal.MaterialExpressionTextureSample,-650,600,texture=textures['T_WorldMap_GroundNormal'],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
link(tile,'',normal,'UVs')
code=r'''
float3 c = Region * (0.94 + 0.12 * Grain.r);
float slope = 1.0 - saturate(N.z);
float rock = smoothstep(0.055, 0.30, slope);
float cold = smoothstep(0.20, 0.55, dot(Region, float3(0.21,0.71,0.08)));
float3 stone = lerp(float3(0.12,0.105,0.083),float3(0.25,0.27,0.28),cold);
c = lerp(c,stone * (0.95 + 0.10 * Grain.r),rock * 0.52);
float elevation = smoothstep(-100.0,6000.0,WP.z);
return c * lerp(0.86,1.055,elevation);
'''
base=custom(code,['Region','Grain','N','WP'],unreal.CustomMaterialOutputType.CMOT_FLOAT3,-150,0)
for a,out,key in [(region,'RGB','Region'),(grain,'RGB','Grain'),(vn,'','N'),(wp,'','WP')]:link(a,out,base,key)
assert mel.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
norm=custom('return normalize(float3(Tex.xy * 0.22, max(Tex.z,0.5)));',['Tex'],unreal.CustomMaterialOutputType.CMOT_FLOAT3,-150,500)
link(normal,'RGB',norm,'Tex');assert mel.connect_material_property(norm,'',unreal.MaterialProperty.MP_NORMAL)
for value,prop,yy in [(0.95,unreal.MaterialProperty.MP_ROUGHNESS,800),(0.15,unreal.MaterialProperty.MP_SPECULAR,950)]:
    n=node(unreal.MaterialExpressionConstant,-150,yy,r=value);assert mel.connect_material_property(n,'',prop)
mel.recompile_material(mat);assert eal.save_loaded_asset(mat)
land=next(a for a in eas.get_all_level_actors() if isinstance(a,unreal.Landscape))
land.set_editor_property('landscape_material',mat)
ticks=0;stage=0;camera=None
output=os.environ.get('RPG_LAND_LOOK_OVERVIEW','')
def finish(delta):
    global ticks,stage,camera,shot
    ticks+=1
    if ticks<180:return
    if stage==0:
        stage=-1  # Saving can pump Slate ticks; prevent reentrant save attempts.
        try:
            assert mel.get_material_property_input_node(mat,unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET) is None
            assert les.save_current_level()
            report=dict(map=MAP,material=ASSETS+'/'+name,terrain_geometry_changed=False,water_changed=False,saved=True)
            (REPORT/'build_report.json').write_text(json.dumps(report,indent=2))
            unreal.log('WORLD_MAP_LAND_LOOK_SAVED '+json.dumps(report))
            if not output:
                unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor();return
            camera=eas.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(0,0,800000),unreal.Rotator(pitch=-90,yaw=-90))
            camera.set_actor_location(unreal.Vector(0,0,800000),False,False)
            camera.set_actor_rotation(unreal.Rotator(pitch=-90,yaw=-90),False)
            cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_field_of_view(90);cc.set_aspect_ratio(14/9)
            shot=unreal.AutomationLibrary.take_high_res_screenshot(2800,1800,output,camera=camera,delay=3.0)
            stage=1
        except Exception:
            unreal.log_error('WORLD_MAP_LAND_LOOK_FAILED '+traceback.format_exc())
            unreal.unregister_slate_post_tick_callback(handle);unreal.SystemLibrary.quit_editor()
    elif stage==1 and Path(output).exists() and shot.is_task_done():
        unreal.unregister_slate_post_tick_callback(handle)
        eas.destroy_actor(camera)
        unreal.log('WORLD_MAP_LAND_LOOK_CAPTURED '+output)
        unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(finish)
