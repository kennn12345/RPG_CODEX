"""Weathers rock shapes inside Unreal (Geometry Script). Most edges get rounded by varying amounts, some patches stay sharp,
edge lines bend, and shallow chips are cut along edges. Each chunk of a rock is a closed island, so each one weathers on its
own. Collision is not touched. Vertex colour alpha stores how much the broad surface faces up, for the moss material.
One test copy (the original is untouched), writes /Game/RPG/Environment/Rocks/Test/SM_RockTest_<name>:
  RPG_WEATHER_ROCK=Rock_Large_A UnrealEditor.exe RPG_CODEX.uproject -ExecutePythonScript="C:/UNREAL PROJECTS/RPG_CODEX/Tools/World/weather_rock.py" -unattended -RenderOffscreen
The whole library, IN PLACE: RPG_WEATHER_ALL=1 (same command). Done meshes get a RockWeathered tag and are skipped on a rerun.
See Docs/ROCK_RECIPE.md."""
import math, os, random
import unreal

LIB = '/Game/RPG/Environment/Rocks/Meshes'
TEST = '/Game/RPG/Environment/Rocks/Test'
CLASSES = ('Large', 'MediumLarge', 'Medium', 'MediumSmall', 'SmallMedium', 'Small')
TAG = 'RockWeathered'
SEED = 1871
EAL = unreal.EditorAssetLibrary
AU, DEF, DEC = unreal.GeometryScript_AssetUtils, unreal.GeometryScript_MeshDeformers, unreal.GeometryScript_MeshDecomposition
Q, ED, NRM = unreal.GeometryScript_MeshQueries, unreal.GeometryScript_MeshEdits, unreal.GeometryScript_Normals
RM, VC = unreal.GeometryScript_Remeshing, unreal.GeometryScript_VertexColors
LISTS = next(getattr(unreal, n) for n in dir(unreal) if n.startswith('GeometryScript_') and hasattr(getattr(unreal, n), 'convert_vector_list_to_array'))


# ---- noise (value noise, pure Python: Unreal's Python has no numpy)
def h3(x, y, z):
    v = math.sin(x * 127.1 + y * 311.7 + z * 74.7) * 43758.5453
    return v - math.floor(v)


def n3(x, y, z):
    ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    ux, uy, uz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
    a = h3(ix, iy, iz) + (h3(ix + 1, iy, iz) - h3(ix, iy, iz)) * ux
    b = h3(ix, iy + 1, iz) + (h3(ix + 1, iy + 1, iz) - h3(ix, iy + 1, iz)) * ux
    c = h3(ix, iy, iz + 1) + (h3(ix + 1, iy, iz + 1) - h3(ix, iy, iz + 1)) * ux
    d = h3(ix, iy + 1, iz + 1) + (h3(ix + 1, iy + 1, iz + 1) - h3(ix, iy + 1, iz + 1)) * ux
    e = a + (b - a) * uy
    return e + (c + (d - c) * uy - e) * uz


def fbm(p, scale, off=0.0):
    x, y, z = p[0] / scale + off, p[1] / scale + off * 1.7, p[2] / scale - off * .6
    return .6 * n3(x, y, z) + .28 * n3(x * 2.1 + 3.1, y * 2.1 + 1.7, z * 2.1 + 5.2) + .12 * n3(x * 4.3 + 7.3, y * 4.3 + 2.9, z * 4.3 + 1.3)


def smooth(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3 - 2 * t)


def to_list(vectors):
    return [(v.x, v.y, v.z) for v in LISTS.convert_vector_list_to_array(vectors)]


def positions(mesh):
    return to_list(Q.get_all_vertex_positions(mesh, False)[1])      # returns (mesh, list, has gaps)


def smoothed(mesh, iterations, with_normals=False):
    copy = unreal.DynamicMesh()
    DEC.copy_mesh_to_mesh(mesh, copy)
    opt = unreal.GeometryScriptIterativeMeshSmoothingOptions()
    opt.set_editor_property('num_iterations', iterations)
    opt.set_editor_property('alpha', 0.5)
    DEF.apply_iterative_smoothing_to_mesh(copy, unreal.GeometryScriptMeshSelection(), opt)
    if not with_normals:
        return positions(copy)
    NRM.set_per_vertex_normals(copy)
    return positions(copy), to_list(NRM.get_mesh_per_vertex_normals(copy)[1])


def weather(name, src_path, dst_path):
    # ---- load
    src = unreal.load_asset(src_path)
    read = unreal.GeometryScriptMeshReadLOD()
    read.set_editor_property('lod_type', unreal.GeometryScriptLODType.SOURCE_MODEL)
    source = unreal.DynamicMesh()
    AU.copy_mesh_from_static_mesh(src, source, unreal.GeometryScriptCopyMeshFromAssetOptions(), read)
    b0 = src.get_bounds()
    ext = b0.box_extent
    size0 = (ext.x * 2, ext.y * 2, ext.z * 2)
    SIZE = max(size0)
    edge_scale = SIZE / 400.0
    S = edge_scale * (.35 if name.startswith('Pebbles') else 1.0)      # every distance below is set for a 4 m rock; pebble clusters are many tiny stones

    # ---- even triangles to shape
    mesh = unreal.DynamicMesh()
    DEC.copy_mesh_to_mesh(source, mesh)
    ro = unreal.GeometryScriptRemeshOptions()
    for k, v in (('discard_attributes', True), ('reproject_to_input_mesh', True), ('remesh_iterations', 20), ('smoothing_rate', 0.2), ('prevent_normal_flips', True)):
        ro.set_editor_property(k, v)
    uo = unreal.GeometryScriptUniformRemeshOptions()
    uo.set_editor_property('target_type', unreal.GeometryScriptUniformRemeshTargetType.TARGET_EDGE_LENGTH)
    uo.set_editor_property('target_edge_length', max(0.6, 3.5 * edge_scale))
    RM.apply_uniform_remesh(mesh, ro, uo)
    VC.transfer_vertex_colors_from_mesh(source, mesh)
    NRM.set_per_vertex_normals(mesh)
    P = positions(mesh)
    N = to_list(NRM.get_mesh_per_vertex_normals(mesh)[1])
    small = smoothed(mesh, 25)
    large, broad_normals = smoothed(mesh, 120, with_normals=True)

    # vertex colour alpha = how much the broad surface here faces up. The moss material multiplies by it, so moss can't land on
    # a tiny upward chip on a steep face. Moss placement itself stays world-based.
    colours = LISTS.convert_color_list_to_array(VC.get_mesh_per_vertex_colors(mesh)[1])
    allowed = [smooth(.42, .66, n[2]) for n in broad_normals]
    VC.set_mesh_per_vertex_colors(mesh, LISTS.convert_array_to_color_list([unreal.LinearColor(c.r, c.g, c.b, a) for c, a in zip(colours, allowed)]))

    # ---- rounding, bends
    rng = random.Random(SEED)
    out, edge, outward = [], [], []
    for i, p in enumerate(P):
        if p == (0.0, 0.0, 0.0):                          # a gap in the vertex IDs
            out.append(p); edge.append(0.0); outward.append((0.0, 0.0, 0.0))
            continue
        a, b, n = small[i], large[i], N[i]
        how_much, which, reach = fbm(p, 90 * S), fbm(p, 60 * S, 11.0), fbm(p, 45 * S, 23.0)
        amount = .12 + .88 * smooth(.34, .52, how_much)   # mostly rounded, some patches left close to sharp
        mix = smooth(.3, .7, which)
        dx, dy, dz = a[0] + (b[0] - a[0]) * mix - p[0], a[1] + (b[1] - a[1]) * mix - p[1], a[2] + (b[2] - a[2]) * mix - p[2]
        length = math.sqrt(dx * dx + dy * dy + dz * dz) + 1e-6
        limit = 13.0 * S * (.55 + .9 * reach)             # no collapsing: thin chunks can't melt away
        move = min(length, limit) * amount / length
        q = [p[0] + dx * move, p[1] + dy * move, p[2] + dz * move]
        bx, by, bz = b[0] - p[0], b[1] - p[1], b[2] - p[2]
        far = math.sqrt(bx * bx + by * by + bz * bz) + 1e-6
        em = min(1.0, far / (10.0 * S))                   # 0 on open faces, 1 on edges and corners
        bend = (fbm(p, 48 * S, 41.0) - .5) * 2.0 * 6.0 * S * em + (fbm(p, 120 * S, 57.0) - .5) * 2.0 * 4.0 * S
        side = ((fbm(p, 42 * S, 71.0) - .5) * 7.0 * S * em, (fbm(p, 42 * S, 83.0) - .5) * 7.0 * S * em, (fbm(p, 42 * S, 97.0) - .5) * 7.0 * S * em)
        out.append((q[0] + n[0] * bend + side[0], q[1] + n[1] * bend + side[1], q[2] + n[2] * bend + side[2]))
        edge.append(em)
        outward.append((-bx / far, -by / far, -bz / far))

    # ---- chips: flat angular bites out of edges
    candidates = [i for i, e in enumerate(edge) if e > .8 and out[i][2] > 12.0 * S]
    rng.shuffle(candidates)
    chips, spacing = [], 42.0 * S
    for i in candidates:
        if len(chips) >= 40:
            break
        c = out[i]
        if all((c[0] - d[0]) ** 2 + (c[1] - d[1]) ** 2 + (c[2] - d[2]) ** 2 > spacing * spacing for d, _, _, _ in chips):
            o = outward[i]
            n = [o[0] + rng.uniform(-.35, .35), o[1] + rng.uniform(-.35, .35), o[2] + rng.uniform(-.35, .35)]
            ln = math.sqrt(sum(v * v for v in n)) + 1e-6
            radius = rng.uniform(10.0, 28.0) * S
            depth = radius * rng.uniform(.1, .24)         # shallow: flat broken faces, not scooped pits
            chips.append((c, [v / ln for v in n], radius, depth))

    # a chip right where two chunks meet leaves a narrow dark hollow, so chips near another chunk are dropped (the rest stay as drawn)
    triangles = LISTS.convert_triangle_list_to_array(Q.get_all_triangle_indices(mesh, False)[1])
    parent = list(range(len(out)))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for t in triangles:
        if t.x < 0:                                       # a gap in the triangle IDs
            continue
        a = root(t.x)
        parent[root(t.y)] = a
        parent[root(t.z)] = a
    chunk = [root(i) for i in range(len(out))]
    at = {out[i]: chunk[i] for i in range(len(out))}
    kept = []
    for chip in chips:
        c, _, radius, _ = chip
        own, reach = at[c], (radius * 1.3 + 6.0 * S) ** 2
        if not any(chunk[i] != own and (p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2 < reach and p != (0.0, 0.0, 0.0) for i, p in enumerate(out)):
            kept.append(chip)
    for c, n, radius, depth in kept:
        q = (c[0] - n[0] * depth, c[1] - n[1] * depth, c[2] - n[2] * depth)
        for i, p in enumerate(out):
            dx, dy, dz = p[0] - c[0], p[1] - c[1], p[2] - c[2]
            dist2 = dx * dx + dy * dy + dz * dz
            if dist2 >= radius * radius:
                continue
            over = (p[0] - q[0]) * n[0] + (p[1] - q[1]) * n[1] + (p[2] - q[2]) * n[2]
            if over > 0:
                fade = over * (1 - smooth(.9, 1.0, math.sqrt(dist2) / radius))
                out[i] = (p[0] - n[0] * fade, p[1] - n[1] * fade, p[2] - n[2] * fade)

    # ---- write
    ED.set_all_mesh_vertex_positions(mesh, LISTS.convert_array_to_vector_list([unreal.Vector(*p) for p in out]))
    split = unreal.GeometryScriptSplitNormalsOptions()
    split.set_editor_property('split_by_opening_angle', True)
    split.set_editor_property('opening_angle_deg', 42.0)                 # rounded areas shade smooth, chips stay crisp
    calc = unreal.GeometryScriptCalculateNormalsOptions()
    calc.set_editor_property('angle_weighted', True); calc.set_editor_property('area_weighted', True)
    NRM.compute_split_normals(mesh, split, calc)

    if dst_path != src_path and not EAL.does_asset_exist(dst_path):
        EAL.duplicate_asset(src_path, dst_path)
    dst = unreal.load_asset(dst_path)
    material = dst.get_material(0)
    wo = unreal.GeometryScriptCopyMeshToAssetOptions()
    for k, v in (('enable_recompute_normals', False), ('enable_recompute_tangents', True), ('enable_remove_degenerates', False), ('emit_transaction', False)):
        wo.set_editor_property(k, v)
    AU.copy_mesh_to_static_mesh(mesh, dst, wo, unreal.GeometryScriptMeshWriteLOD())
    if material and dst.get_material(0) != material:
        dst.set_material(0, material)
    if dst_path == src_path:
        EAL.set_metadata_tag(dst, TAG, '1')
    EAL.save_loaded_asset(dst, only_if_is_dirty=False)

    dst = unreal.load_asset(dst_path)
    e1 = dst.get_bounds().box_extent
    hulls = len(dst.get_editor_property('body_setup').get_editor_property('agg_geom').get_editor_property('convex_elems'))
    worst = max(abs(a - b) / max(b, 1e-3) for a, b in zip((e1.x * 2, e1.y * 2, e1.z * 2), size0))
    mat = dst.get_material(0)
    unreal.log(f'WEATHER {name} verts={len(P)} islands={Q.get_num_connected_components(mesh)} open_loops={Q.get_num_open_border_loops(mesh)[0]} chips={len(kept)}/{len(chips)} '
               f'hulls={hulls} nanite={dst.get_editor_property("nanite_settings").enabled} material={mat.get_name() if mat else None} '
               f'size={size0[0]:.0f}x{size0[1]:.0f}x{size0[2]:.0f}->{e1.x * 2:.0f}x{e1.y * 2:.0f}x{e1.z * 2:.0f} change={worst * 100:.1f}%')


if os.environ.get('RPG_WEATHER_ALL') == '1':
    names = [f'Rock_{c}_{v}' for c in CLASSES for v in 'ABC'] + [f'Pebbles_{v}' for v in 'ABC']
    done = failed = 0
    for name in names:
        path = f'{LIB}/SM_{name}'
        if EAL.get_metadata_tag(unreal.load_asset(path), TAG) == '1':
            unreal.log(f'WEATHER {name} already weathered, skipped')
            continue
        try:
            weather(name, path, path)
            done += 1
        except Exception as ex:
            failed += 1
            unreal.log_error(f'WEATHER_FAILED {name}: {ex}')
    unreal.log(f'WEATHER_ALL done={done} failed={failed}')
else:
    name = os.environ.get('RPG_WEATHER_ROCK', 'Rock_Large_A')
    weather(name, f'{LIB}/SM_{name}', f'{TEST}/SM_RockTest_{name}')
unreal.SystemLibrary.quit_editor()
