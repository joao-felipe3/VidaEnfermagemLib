"""Cria morph targets de boca, olhos e expressão no SK_Remy (rig Mixamo sem osso de mandíbula e olhos).

Rodar com o Blender em modo background:
    blender -b --factory-startup --python criar_visemas_remy.py -- <SK_Remy.fbx> <saida.fbx> [pasta_previews]

Morph targets gerados (todos 0..1):
    JawOpen        abre a mandíbula (pele, lábio inferior, dentes e gengiva de baixo e língua)
    MouthPucker    bico (O, U)
    MouthWide      lábios esticados (I, E, S)
    LowerLipIn     lábio inferior sob os dentes de cima (F, V)
    LipsPress      lábios fechados e pressionados (M, B, P)
    UpperLipUp     lábio superior sobe e mostra os dentes (S, E, F)
    TongueUp       ponta da língua atrás dos dentes de cima (L, T, D, N)
    Blink          pálpebras fechadas perfeitamente esféricas com cílios síncronos
    EyeLookLeft    globo ocular vira para a esquerda do paciente (-X)
    EyeLookRight   globo ocular vira para a direita do paciente (+X)
    EyeLookUp      globo ocular olha para cima (+Z)
    EyeLookDown    globo ocular olha para baixo (-Z)
    BrowUp         sobrancelhas e testa erguidas (surpresa, ênfase na fala, saudação)
    BrowDown       sobrancelhas franzidas anatomicamente (preocupação, dor, concentração)
    Smile          sorriso suave e quente com bochechas elevadas e duchenne squint
    Squint         tensão/contração dos olhos (dor, claridade, exame clínico)
"""

import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC = argv[0] if len(argv) > 0 else "Tools/LipSync/SK_Remy.fbx"
DST = argv[1] if len(argv) > 1 else "Tools/LipSync/SK_Remy_Visemas.fbx"
PREVIEW_DIR = os.path.abspath(argv[2]) if len(argv) > 2 else None

# Pontos de referência do rosto (espaço do mundo após importar o FBX)
SEAM_Z = 3.357                        # linha entre os lábios
MOUTH_CENTER = Vector((0.0, -0.245, SEAM_Z))
MOUTH_HALF_WIDTH = 0.062              # cantos da boca
JAW_PIVOT = Vector((0.0, -0.050, 3.420))
JAW_ANGLE = math.radians(16.0)
UPPER_Z = 3.350                       # centróides acima disto ficam com o crânio (dentes, gengiva)
TONGUE_Z = (3.350, 3.360)             # faixa do centróide da língua entre as ilhas grandes
EYE_REGION_Z = (3.44, 3.56)           # faixa de altura dos olhos


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


def eye_parts(me, W):
    eyes, small = [], []
    for verts in islands_of(me):
        pts = [W[i] for i in verts]
        c = sum(pts, Vector()) / len(pts)
        if not (0.02 < abs(c.x) < 0.13 and EYE_REGION_Z[0] < c.z < EYE_REGION_Z[1] and c.y < -0.15):
            continue
        if 200 <= len(verts) <= 300:
            r = (max(p.z for p in pts) - min(p.z for p in pts)) / 2.0
            # Centro esférico real
            center = Vector((c.x, min(p.y for p in pts) + r, c.z))
            eyes.append((center, r, verts))
        elif len(verts) in (19, 30) and c.y < -0.21:
            small.append((verts, c))
    lashes = {}
    for verts, c in small:
        center, r, _ = min(eyes, key=lambda e: abs(e[0].x - c.x))
        # Cílios ficam diretamente na abertura do olho (Z de 3.47 a 3.51)
        if abs(c.z - center.z) > 0.40 * r or abs(c.x - center.x) > 1.25 * r:
            continue
        for i in verts:
            lashes[i] = (center, r, "upper" if c.z > center.z else "lower")
    return eyes, lashes


def in_face(p):
    return abs(p.x) < 0.16 and 3.15 < p.z < 3.45 and p.y < 0.02


def surface_distances(me, W, seeds):
    adj = {}
    for e in me.edges:
        a, b = e.vertices
        if in_face(W[a]) and in_face(W[b]):
            d = (W[a] - W[b]).length
            adj.setdefault(a, []).append((b, d))
            adj.setdefault(b, []).append((a, d))
    import heapq
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
    w *= smoothstep(3.19, 3.25, p.z)
    w *= 1.0 - smoothstep(0.10, 0.15, ax)
    w *= 1.0 - smoothstep(-0.10, -0.02, p.y)
    return w


def lip_weight(p, rx=0.075, rz=0.030):
    if p.y > -0.19:
        return 0.0
    d = math.sqrt((p.x / rx) ** 2 + ((p.z - SEAM_Z) / rz) ** 2)
    return 1.0 - smoothstep(0.45, 1.0, d)


def rotate_about_x(p, center, angle):
    c = math.cos(angle)
    s = math.sin(angle)
    y = p.y - center.y
    z = p.z - center.z
    return Vector((p.x, center.y + y * c - z * s, center.z + y * s + z * c))


def rotate_about_z(p, center, angle):
    c = math.cos(angle)
    s = math.sin(angle)
    x = p.x - center.x
    y = p.y - center.y
    return Vector((center.x + x * c - y * s, center.y + x * s + y * c, p.z))


def build(o):
    me = o.data
    M = o.matrix_world
    Mi = M.inverted()
    W = [M @ v.co for v in me.vertices]
    interior = mouth_interior_islands(me, W)
    side = jaw_side(me, W)
    eyes, lashes = eye_parts(me, W)

    eyeball_verts = set()
    for _, _, v_list in eyes:
        eyeball_verts.update(v_list)

    hair = {v for poly in me.polygons if "hair" in me.materials[poly.material_index].name.lower()
            for v in poly.vertices}

    if not me.shape_keys:
        o.shape_key_add(name="Basis", from_mix=False)

    rot_jaw = (Matrix.Translation(JAW_PIVOT) @ Matrix.Rotation(JAW_ANGLE, 4, "X")
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

    # --- VISEMAS DE FALA / BOCA ---
    def jaw(i, p):
        kind = interior.get(i)
        if kind == "upper":
            return None
        if not kind and lip_weight(p) and not is_lower_lip(i):
            return p + Vector((0.0, 0.0, 0.003 * lip_weight(p)))
        w = {"lower": 1.0, "tongue": 0.75}.get(kind) if kind else jaw_weight(p, side.get(i))
        if not w:
            return None
        q = p.lerp(rot_jaw @ p, w)
        if not kind:
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
        w = smoothstep(tongue_tip_y + 0.12, tongue_tip_y + 0.01, p.y)
        return Vector((p.x, p.y + 0.004 * w, p.z + 0.018 * w))

    # --- PÁLPEBRAS (BLINK) ANATÔMICO ESFÉRICO PERFEITO ---
    UPPER_BLINK_ANGLE = math.radians(33.5)
    LOWER_BLINK_ANGLE = math.radians(-8.5)

    def blink(i, p):
        if i in lashes:
            center, r, which = lashes[i]
            u = (p.x - center.x) / (1.05 * r)
            w_x = math.cos(min(abs(u), 1.0) * math.pi * 0.5) ** 1.3
            ang = UPPER_BLINK_ANGLE * w_x if which == "upper" else LOWER_BLINK_ANGLE * w_x
            return rotate_about_x(p, center, ang)

        if i in eyeball_verts or i in interior or i in hair or p.y > -0.16:
            return None

        center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
        dx = p.x - center.x
        dz = p.z - center.z
        u = dx / (1.05 * r)
        if abs(u) >= 1.0:
            return None
        w_x = math.cos(abs(u) * math.pi * 0.5) ** 1.3

        if -0.10 * r <= dz <= 0.65 * r:
            w_z = 1.0 - smoothstep(0.20 * r, 0.60 * r, dz)
            w = w_x * w_z
            if w > 0.001:
                return rotate_about_x(p, center, UPPER_BLINK_ANGLE * w)
        elif -0.65 * r <= dz < -0.10 * r:
            w_z = 1.0 - smoothstep(0.12 * r, 0.55 * r, -dz)
            w = w_x * w_z
            if w > 0.001:
                return rotate_about_x(p, center, LOWER_BLINK_ANGLE * w)
        return None

    # --- OLHOS: ROTAÇÃO DO GLOBO OCULAR (EYE GAZE MORPHS) ---
    GAZE_YAW_ANGLE = math.radians(18.0)
    GAZE_PITCH_ANGLE = math.radians(14.0)

    def eye_look_left(i, p):
        if i not in eyeball_verts:
            return None
        center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
        return rotate_about_z(p, center, -GAZE_YAW_ANGLE)

    def eye_look_right(i, p):
        if i not in eyeball_verts:
            return None
        center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
        return rotate_about_z(p, center, GAZE_YAW_ANGLE)

    def eye_look_up(i, p):
        if i not in eyeball_verts:
            return None
        center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
        return rotate_about_x(p, center, -GAZE_PITCH_ANGLE)

    def eye_look_down(i, p):
        if i not in eyeball_verts:
            return None
        center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
        return rotate_about_x(p, center, GAZE_PITCH_ANGLE)

    # --- EXPRESSÕES FACIAIS VÍVIDAS E ANATÔMICAS ---
    def brow_up(i, p):
        if i in lashes or i in eyeball_verts or i in interior or i in hair or p.y > -0.10:
            return None
        if not (3.50 < p.z < 3.68 and abs(p.x) < 0.11 and p.y < -0.17):
            return None
        w_x = 1.0 - smoothstep(0.06, 0.11, abs(p.x))
        w_z = smoothstep(3.50, 3.54, p.z) * (1.0 - smoothstep(3.58, 3.67, p.z))
        w = w_x * w_z
        if w <= 0.001:
            return None
        return p + Vector((0.0, 0.002 * w, 0.0125 * w))

    def brow_down(i, p):
        if i in lashes or i in eyeball_verts or i in interior or i in hair or p.y > -0.10:
            return None
        if not (3.50 < p.z < 3.62 and abs(p.x) < 0.09 and p.y < -0.18):
            return None
        ax = abs(p.x)
        w_spread = 1.0 - smoothstep(0.05, 0.08, ax)
        w_z = smoothstep(3.50, 3.53, p.z) * (1.0 - smoothstep(3.56, 3.62, p.z))
        w = w_spread * w_z
        if w <= 0.001:
            return None
        inward_scale = smoothstep(0.002, 0.035, ax)
        dx = - (p.x / max(ax, 1e-5)) * 0.008 * w * inward_scale
        dy = -0.0025 * w
        dz = -0.010 * w
        return p + Vector((dx, dy, dz))

    def smile(i, p):
        if i in interior or i in lashes or i in eyeball_verts or i in hair or p.y > -0.10:
            return None
        corner_x = math.copysign(MOUTH_HALF_WIDTH, p.x)
        d_corner = math.sqrt(((p.x - corner_x)/0.045)**2 + ((p.y - (-0.235))/0.035)**2 + ((p.z - SEAM_Z)/0.035)**2)
        w_corner = 1.0 - smoothstep(0.0, 1.0, d_corner) if p.y < -0.20 else 0.0

        cheek_pos = Vector((math.copysign(0.065, p.x), -0.210, 3.425))
        d_cheek = (p - cheek_pos).length
        w_cheek = 1.0 - smoothstep(0.0, 0.055, d_cheek) if p.y < -0.18 else 0.0

        w_eye_squint = 0.0
        center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
        dx = p.x - center.x
        dz = p.z - center.z
        if abs(dx) < 1.05 * r and -0.65 * r <= dz < -0.10 * r and p.y < -0.18:
            u = dx / (1.05 * r)
            w_eye_squint = (math.cos(abs(u) * math.pi * 0.5) ** 1.3) * (1.0 - smoothstep(0.15 * r, 0.55 * r, -dz))

        if not (w_corner or w_cheek or w_eye_squint):
            return None

        disp_x = math.copysign(0.006 * w_corner + 0.003 * w_cheek, p.x)
        disp_y = 0.004 * w_corner - 0.004 * w_cheek - 0.002 * w_eye_squint
        disp_z = 0.010 * w_corner + 0.008 * w_cheek + 0.003 * w_eye_squint
        return p + Vector((disp_x, disp_y, disp_z))

    def squint(i, p):
        if i in interior or i in lashes or i in eyeball_verts or i in hair or p.y > -0.12:
            return None
        center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
        dx = p.x - center.x
        dz = p.z - center.z
        if abs(dx) < 1.1 * r and -0.70 * r <= dz < 0.10 * r and p.y < -0.18:
            u = dx / (1.1 * r)
            w_x = math.cos(min(abs(u), 1.0) * math.pi * 0.5) ** 1.3
            w_z = 1.0 - smoothstep(0.10 * r, 0.60 * r, -dz)
            w = w_x * w_z
            if w > 0.001:
                return p + Vector((0.0, -0.002 * w, 0.005 * w))
        return None

    add_key("JawOpen", jaw)
    add_key("MouthPucker", pucker)
    add_key("MouthWide", wide)
    add_key("LowerLipIn", lower_lip_in)
    add_key("LipsPress", lips_press)
    add_key("UpperLipUp", upper_lip_up)
    add_key("TongueUp", tongue_up)
    add_key("Blink", blink)
    add_key("EyeLookLeft", eye_look_left)
    add_key("EyeLookRight", eye_look_right)
    add_key("EyeLookUp", eye_look_up)
    add_key("EyeLookDown", eye_look_down)
    add_key("BrowUp", brow_up)
    add_key("BrowDown", brow_down)
    add_key("Smile", smile)
    add_key("Squint", squint)
    print("VISEMAS olhos", [(tuple(round(v, 3) for v in c), round(r, 3)) for c, r, _ in eyes], "cílios", len(lashes))


def render_previews(o, folder):
    os.makedirs(folder, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.light = "STUDIO"
    scene.display.shading.show_cavity = True
    scene.render.resolution_x = scene.render.resolution_y = 600
    center = MOUTH_CENTER + Vector((0, 0, -0.005))
    eyes_center = Vector((0.0, -0.20, 3.50))

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
        "Blink": {"Blink": 1.0},
        "BlinkMeio": {"Blink": 0.5},
        "BrowUp": {"BrowUp": 1.0},
        "BrowDown": {"BrowDown": 1.0},
        "Smile": {"Smile": 1.0},
        "Squint": {"Squint": 1.0},
        "Preocupado": {"BrowDown": 0.7, "Squint": 0.4},
        "FalandoFeliz": {"JawOpen": 0.4, "Smile": 0.6, "BrowUp": 0.5},
    }
    keys = o.data.shape_keys.key_blocks
    views = (
        ("frente", Vector((0, -1, 0)), None, center, 0.30),
        ("lado", Vector((1, -0.35, 0)).normalized(), None, center, 0.30),
        ("aluno", Vector((0.25, -0.9, 0.15)).normalized(), 85, center, 0.30),
        ("olhos", Vector((0.05, -1, 0.02)).normalized(), None, eyes_center, 0.22),
    )
    for view, direction, lens, target, scale in views:
        cam_data = bpy.data.cameras.new(view)
        if lens:
            cam_data.lens = lens
        else:
            cam_data.type = "ORTHO"
            cam_data.ortho_scale = scale
        cam = bpy.data.objects.new(view, cam_data)
        scene.collection.objects.link(cam)
        cam.location = target + direction * (0.95 if lens else 1.0)
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
