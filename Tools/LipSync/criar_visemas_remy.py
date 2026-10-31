"""Cria morph targets de boca no SK_Remy (rig Mixamo sem osso de mandíbula).

Rodar com o Blender em modo background:
    blender -b --factory-startup --python criar_visemas_remy.py -- <SK_Remy.fbx> <saida.fbx> [pasta_previews]

Morph targets gerados (todos 0..1):
    JawOpen      abre a mandíbula (pele, lábio inferior, dentes e gengiva de baixo e língua)
    MouthPucker  bico (O, U)
    MouthWide    lábios esticados (I, E, S)
    LowerLipIn   lábio inferior sob os dentes de cima (F, V)
    LipsPress    lábios fechados e pressionados (M, B, P)
    UpperLipUp   lábio superior sobe e mostra os dentes (S, E, F)
    TongueUp     ponta da língua atrás dos dentes de cima (L, T, D, N)

As coordenadas abaixo foram medidas no FBX exportado do SK_Remy da Unreal.
"""

import heapq
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC = argv[0]
DST = argv[1]
PREVIEW_DIR = os.path.abspath(argv[2]) if len(argv) > 2 else None

# Pontos de referência do rosto (espaço do mundo após importar o FBX)
SEAM_Z = 3.357                        # linha entre os lábios
MOUTH_CENTER = Vector((0.0, -0.245, SEAM_Z))
MOUTH_HALF_WIDTH = 0.062              # cantos da boca (o FBX está em ~2x a escala real)
JAW_PIVOT = Vector((0.0, -0.050, 3.420))
JAW_ANGLE = math.radians(16.0)
UPPER_Z = 3.350                       # centróides acima disto ficam com o crânio (dentes, gengiva)
TONGUE_Z = (3.350, 3.360)             # faixa do centróide da língua entre as ilhas grandes


def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def islands_of(me):
    parent = list(range(len(me.vertices)))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for e in me.edges:
        a, b = find(e.vertices[0]), find(e.vertices[1])
        if a != b:
            parent[a] = b
    groups = {}
    for i in range(len(me.vertices)):
        groups.setdefault(find(i), []).append(i)
    return list(groups.values())


def mouth_interior_islands(me, W):
    """Ilhas inteiramente dentro da boca: cada dente é uma ilha; as grandes (>150 vértices) são a
    gengiva/céu da boca de cima, a língua e a gengiva/assoalho de baixo, separadas pelo centróide."""
    def inside(p):
        return abs(p.x) < 0.09 and 3.28 < p.z < 3.42 and -0.25 < p.y < -0.03

    result = {}
    for verts in islands_of(me):
        if len(verts) < 3000 and all(inside(W[i]) for i in verts):
            z = sum(W[i].z for i in verts) / len(verts)
            if len(verts) > 150 and TONGUE_Z[0] < z <= TONGUE_Z[1]:
                kind = "tongue"
            elif z > UPPER_Z:
                kind = "upper"
            else:
                kind = "lower"
            for i in verts:
                result[i] = kind
    return result


def in_face(p):
    return abs(p.x) < 0.16 and 3.15 < p.z < 3.45 and p.y < 0.02


def surface_distances(me, W, seeds):
    """Dijkstra pelas arestas da malha, limitado ao rosto."""
    adj = {}
    for e in me.edges:
        a, b = e.vertices
        if in_face(W[a]) and in_face(W[b]):
            d = (W[a] - W[b]).length
            adj.setdefault(a, []).append((b, d))
            adj.setdefault(b, []).append((a, d))
    dist = {s: 0.0 for s in seeds}
    heap = [(0.0, s) for s in seeds]
    heapq.heapify(heap)
    while heap:
        d, v = heapq.heappop(heap)
        if d > dist.get(v, math.inf):
            continue
        for n, w in adj.get(v, ()):
            nd = d + w
            if nd < dist.get(n, math.inf):
                dist[n] = nd
                heapq.heappush(heap, (nd, n))
    return dist


def jaw_side(me, W):
    """0 = acompanha o nariz (fixo), 1 = acompanha o queixo.

    Os lábios se sobrepõem em altura (o de cima termina abaixo de onde o de baixo começa),
    então a separação é feita pela distância ao longo da pele: do lábio inferior até o nariz
    o caminho precisa contornar o canto da boca.
    """
    upper = [i for i, p in enumerate(W) if in_face(p) and p.z > 3.385 and p.y < -0.20 and abs(p.x) < 0.08]
    lower = [i for i, p in enumerate(W) if in_face(p) and p.z < 3.30 and p.y < -0.18 and abs(p.x) < 0.08]
    d_up = surface_distances(me, W, upper)
    d_low = surface_distances(me, W, lower)
    side = {}
    for i in set(d_up) & set(d_low):
        total = d_up[i] + d_low[i]
        side[i] = d_up[i] / total if total > 0 else 0.5
    return side


def jaw_weight(p, side):
    ax = abs(p.x)
    if side is None or not (3.18 < p.z < 3.42 and p.y < 0.0 and ax < 0.16):
        return 0.0
    w = smoothstep(0.40, 0.60, side)
    w *= smoothstep(3.19, 3.25, p.z)                # pescoço sob o queixo
    w *= 1.0 - smoothstep(0.10, 0.15, ax)            # ângulo da mandíbula
    w *= 1.0 - smoothstep(-0.10, -0.02, p.y)         # parte de trás da cabeça
    return w


def lip_weight(p, rx=0.075, rz=0.030):
    if p.y > -0.19:
        return 0.0
    d = math.sqrt((p.x / rx) ** 2 + ((p.z - SEAM_Z) / rz) ** 2)
    return 1.0 - smoothstep(0.45, 1.0, d)


def build(o):
    me = o.data
    M = o.matrix_world
    Mi = M.inverted()
    W = [M @ v.co for v in me.vertices]
    interior = mouth_interior_islands(me, W)
    side = jaw_side(me, W)

    if not me.shape_keys:
        o.shape_key_add(name="Basis", from_mix=False)

    rot = (Matrix.Translation(JAW_PIVOT) @ Matrix.Rotation(JAW_ANGLE, 4, "X")
           @ Matrix.Translation(-JAW_PIVOT))

    def add_key(name, displace):
        key = o.shape_key_add(name=name, from_mix=False)
        for i, p in enumerate(W):
            q = displace(i, p)
            if q is not None:
                key.data[i].co = Mi @ q
        return key

    def is_lower_lip(i):
        return side.get(i, 0.0) >= 0.5

    def jaw(i, p):
        kind = interior.get(i)
        if kind == "upper":
            return None
        if not kind and lip_weight(p) and not is_lower_lip(i):
            # O lábio de cima sobe um pouco quando a boca abre
            return p + Vector((0.0, 0.0, 0.003 * lip_weight(p)))
        w = {"lower": 1.0, "tongue": 0.75}.get(kind) if kind else jaw_weight(p, side.get(i))
        if not w:
            return None
        q = p.lerp(rot @ p, w)
        if not kind:
            # O lábio de baixo cai um pouco mais que o queixo e vira levemente para fora
            lw = lip_weight(p, rx=0.07, rz=0.03) * w
            q += Vector((0.0, -0.004 * lw, -0.005 * lw))
        return q

    def pucker(i, p):
        if i in interior:
            return None
        w = lip_weight(p)
        if not w:
            return None
        return Vector((p.x * (1.0 - 0.42 * w), p.y - 0.022 * w, p.z + (SEAM_Z - p.z) * 0.10 * w))

    def wide(i, p):
        if i in interior:
            return None
        w = lip_weight(p, rx=0.085, rz=0.028)
        if not w:
            return None
        stretch = 0.012 * w * min(abs(p.x) / MOUTH_HALF_WIDTH, 1.0)
        return Vector((p.x + math.copysign(stretch, p.x), p.y + 0.005 * w, p.z))

    def lower_lip_in(i, p):
        if i in interior or side.get(i, 0.0) < 0.5:
            return None
        w = lip_weight(p, rx=0.065, rz=0.026)
        if not w:
            return None
        return Vector((p.x, p.y + 0.012 * w, p.z + 0.008 * w))

    def lips_press(i, p):
        if i in interior:
            return None
        w = lip_weight(p, rx=0.07, rz=0.026)
        if not w:
            return None
        dz = 0.0025 * w if is_lower_lip(i) else -0.0015 * w
        return Vector((p.x * (1.0 - 0.03 * w), p.y + 0.003 * w, p.z + dz))

    def upper_lip_up(i, p):
        if i in interior or is_lower_lip(i):
            return None
        w = lip_weight(p, rx=0.065, rz=0.03)
        if not w:
            return None
        return Vector((p.x, p.y - 0.002 * w, p.z + 0.008 * w))

    tongue_tip_y = min((W[i].y for i, k in interior.items() if k == "tongue"), default=0.0)

    def tongue_up(i, p):
        if interior.get(i) != "tongue":
            return None
        w = smoothstep(tongue_tip_y + 0.12, tongue_tip_y + 0.01, p.y)   # só a parte da frente
        return Vector((p.x, p.y + 0.004 * w, p.z + 0.018 * w))

    add_key("JawOpen", jaw)
    add_key("MouthPucker", pucker)
    add_key("MouthWide", wide)
    add_key("LowerLipIn", lower_lip_in)
    add_key("LipsPress", lips_press)
    add_key("UpperLipUp", upper_lip_up)
    add_key("TongueUp", tongue_up)


def render_previews(o, folder):
    os.makedirs(folder, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.show_cavity = True
    scene.render.resolution_x = scene.render.resolution_y = 600
    center = MOUTH_CENTER + Vector((0, 0, -0.005))
    # Uma pose por forma do Rhubarb, com os mesmos pesos do UPatientVoiceComponent
    poses = {
        "neutro": {},
        "A_MBP": {"LipsPress": 1.0},
        "B_KST": {"JawOpen": 0.10, "MouthWide": 0.35, "UpperLipUp": 0.45, "TongueUp": 0.2},
        "C_E": {"JawOpen": 0.35, "MouthWide": 0.30, "UpperLipUp": 0.25},
        "D_A": {"JawOpen": 0.75, "MouthWide": 0.10, "UpperLipUp": 0.10},
        "E_O": {"JawOpen": 0.35, "MouthPucker": 0.50},
        "F_U": {"JawOpen": 0.12, "MouthPucker": 0.95},
        "G_FV": {"JawOpen": 0.06, "MouthWide": 0.10, "LowerLipIn": 0.90, "UpperLipUp": 0.35},
        "H_L": {"JawOpen": 0.35, "MouthWide": 0.10, "UpperLipUp": 0.10, "TongueUp": 1.0},
    }
    keys = o.data.shape_keys.key_blocks
    # "frente" e "lado" ortográficos; "aluno" em perspectiva, de onde o estudante olha o paciente
    views = (("frente", Vector((0, -1, 0)), None), ("lado", Vector((1, -0.35, 0)).normalized(), None),
             ("aluno", Vector((0.25, -0.9, 0.15)).normalized(), 85))
    for view, direction, lens in views:
        cam_data = bpy.data.cameras.new(view)
        if lens:
            cam_data.lens = lens
        else:
            cam_data.type = "ORTHO"
            cam_data.ortho_scale = 0.30
        cam = bpy.data.objects.new(view, cam_data)
        scene.collection.objects.link(cam)
        cam.location = center + direction * (0.95 if lens else 1.0)
        cam.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
        scene.camera = cam
        for pose, values in poses.items():
            for k in keys[1:]:
                k.value = values.get(k.name, 0.0)
            scene.render.filepath = os.path.join(folder, f"{view}_{pose}.png")
            bpy.ops.render.render(write_still=True)
    for k in keys[1:]:
        k.value = 0.0


bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=SRC)
mesh = next(x for x in bpy.data.objects if x.type == "MESH")
build(mesh)
if PREVIEW_DIR:
    render_previews(mesh, PREVIEW_DIR)
bpy.ops.export_scene.fbx(
    filepath=DST,
    use_selection=False,
    object_types={"ARMATURE", "MESH"},
    add_leaf_bones=False,
    bake_anim=False,
    use_mesh_modifiers=False,
    mesh_smooth_type="FACE",
)
print("VISEMAS OK", [k.name for k in mesh.data.shape_keys.key_blocks])
