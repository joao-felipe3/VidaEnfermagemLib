"""Gera o modelo SK_Remy_Visemas.fbx com anatomia labial humana e 16 morph targets orgânicos.
Garante que APENAS vértices da cabeça e cavidade oral se movem, com zero retração do corpo.

Execução via Blender em modo background:
    blender -b -P Tools/LipSync/criar_visemas_remy.py
"""

import math
import os
import sys
import bpy
import bmesh
from mathutils import Vector, Matrix

SRC = "Tools/LipSync/SK_Remy.fbx"
DST = "Tools/LipSync/SK_Remy_Visemas.fbx"
OUTPUT_DIR = "C:/Users/GILSON/.gemini/antigravity/brain/8c760046-4c51-40ce-8391-7855049e753e"

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=SRC)
mesh = next(x for x in bpy.data.objects if x.type == "MESH")
me = mesh.data
M = mesh.matrix_world
Mi = M.inverted()

# 1. IDENTIFICAR O ISLAND DA CABEÇA E AS ILHAS INTERNAS
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

W_orig = [M @ v.co for v in me.vertices]
head_root = None
for i, p in enumerate(W_orig):
    if abs(p.x) < 0.01 and 3.55 < p.z < 3.60 and p.y < -0.20:
        head_root = find(i)
        break

head_verts = set(i for i in range(len(me.vertices)) if find(i) == head_root)

def inside_mouth(p):
    return abs(p.x) < 0.09 and 3.28 < p.z < 3.42 and -0.25 < p.y < -0.03

islands = {}
for i in range(len(me.vertices)):
    islands.setdefault(find(i), []).append(i)

interior_kinds = {}
teeth_verts = set()
tongue_verts = set()

for root, v_list in islands.items():
    if root == head_root:
        continue
    pts = [W_orig[i] for i in v_list]
    if len(v_list) < 3000 and all(inside_mouth(pt) for pt in pts):
        z = sum(pt.z for pt in pts) / len(pts)
        if len(v_list) > 150 and 3.350 < z <= 3.360:
            kind = "tongue"
            tongue_verts.update(v_list)
        elif z > 3.350:
            kind = "upper"
            if len(v_list) < 150:
                teeth_verts.update(v_list)
        else:
            kind = "lower"
            if len(v_list) < 150:
                teeth_verts.update(v_list)
        for i in v_list:
            interior_kinds[i] = kind

# Olhos e cílios
EYE_REGION_Z = (3.44, 3.56)
eyes, small = [], []
for verts in islands.values():
    pts = [W_orig[i] for i in verts]
    c = sum(pts, Vector()) / len(pts)
    if not (0.02 < abs(c.x) < 0.13 and EYE_REGION_Z[0] < c.z < EYE_REGION_Z[1] and c.y < -0.15):
        continue
    if 200 <= len(verts) <= 300:
        r = (max(p.z for p in pts) - min(p.z for p in pts)) / 2.0
        center = Vector((c.x, min(p.y for p in pts) + r, c.z))
        eyes.append((center, r, verts))
    elif len(verts) in (19, 30) and c.y < -0.21:
        small.append((verts, c))

lashes = {}
for verts, c in small:
    center, r, _ = min(eyes, key=lambda e: abs(e[0].x - c.x))
    if abs(c.z - center.z) > 0.40 * r or abs(c.x - center.x) > 1.25 * r:
        continue
    for i in verts:
        lashes[i] = (center, r, "upper" if c.z > center.z else "lower")

eyeball_verts = set()
for _, _, v_list in eyes:
    eyeball_verts.update(v_list)

hair = {v for poly in me.polygons if "hair" in me.materials[poly.material_index].name.lower() for v in poly.vertices}

# 2. INCISÃO ANATÔMICA ORGÂNICA DA RIMA ORIS (SEPARANDO OS LÁBIOS)
CORNER_X = 0.052

bm = bmesh.new()
bm.from_mesh(me)
bm.verts.ensure_lookup_table()
bm.edges.ensure_lookup_table()
bm.faces.ensure_lookup_table()

lip_faces = []
for f in bm.faces:
    if any(v.index in head_verts for v in f.verts):
        pts = [M @ v.co for v in f.verts]
        if all(abs(p.x) < CORNER_X and p.y < -0.20 and 3.340 < p.z < 3.375 for p in pts):
            lip_faces.append(f)

lip_edges = list(set(e for f in lip_faces for e in f.edges))
local_plane_co = Mi @ Vector((0, -0.23, 3.3565))
local_plane_no = Vector((0, 0, 1))

res = bmesh.ops.bisect_plane(bm, geom=lip_faces + lip_edges, plane_co=local_plane_co, plane_no=local_plane_no)
cut_edges = [e for e in res["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]

inner_cut_edges = []
for e in cut_edges:
    pts = [M @ v.co for v in e.verts]
    if all(abs(p.x) < CORNER_X - 0.003 for p in pts):
        inner_cut_edges.append(e)

bmesh.ops.split_edges(bm, edges=inner_cut_edges)

for v in bm.verts:
    v.tag = False

for f in bm.faces:
    pts = [M @ v.co for v in f.verts]
    c_z = sum(p.z for p in pts) / len(pts)
    c_y = sum(p.y for p in pts) / len(pts)
    c_x = sum(abs(p.x) for p in pts) / len(pts)
    if c_x < 0.07 and c_y < -0.20 and 3.32 < c_z < 3.3565:
        for v in f.verts:
            if (M @ v.co).z <= 3.3565:
                v.tag = True

# 3. ESCULTURA ANATÔMICA PLÁSTICA DOS LÁBIOS (VOLUME 3D ORGANIC HARMONIC)
for v in bm.verts:
    p = M @ v.co
    ax = abs(p.x)
    if ax < 0.075 and p.y < -0.20 and 3.330 < p.z < 3.385:
        if not v.tag: # Lábio superior
            dist_seam = max(p.z - 3.3565, 0.0)
            w_lip = math.cos(min(dist_seam / 0.024, 1.0) * math.pi * 0.5) ** 1.3
            g_cupid = math.exp(-((ax - 0.011) ** 2) / (2 * (0.0055 ** 2)))
            g_tubercle = math.exp(-(ax ** 2) / (2 * (0.0048 ** 2)))
            g_lateral = math.exp(-((ax - 0.026) ** 2) / (2 * (0.012 ** 2)))
            
            dz = (0.0009 * g_cupid - 0.0005 * g_tubercle) * w_lip
            dy = -(0.0018 * g_tubercle + 0.0016 * g_cupid + 0.0010 * g_lateral) * w_lip
            p.z += dz
            p.y += dy
        else: # Lábio inferior
            dist_seam = max(3.3565 - p.z, 0.0)
            w_lip = math.cos(min(dist_seam / 0.024, 1.0) * math.pi * 0.5) ** 1.3
            g_lobe = math.exp(-((ax - 0.016) ** 2) / (2 * (0.011 ** 2)))
            dy = -(0.0026 * g_lobe + 0.0012) * w_lip * (1.0 - ax / 0.075)
            dz = -0.0005 * g_lobe * w_lip
            p.y += dy
            p.z += dz
            
        # Comissuras / Modíolo
        if 0.038 < ax < 0.062 and 3.350 < p.z < 3.365:
            w_corner = math.sin(min(max((ax - 0.038) / 0.024, 0.0), 1.0) * math.pi)
            p.y += 0.0024 * w_corner
            
        v.co = Mi @ p

for f in bm.faces:
    f.smooth = True

lower_indices = {v.index for v in bm.verts if v.tag}
# Atualizar head_verts com os novos índices criados na incisão
head_verts_updated = {v.index for v in bm.verts if (M @ v.co).z > 3.10 and abs((M @ v.co).x) < 0.20}

bm.to_mesh(me)
me.update()
bm.free()

if hasattr(me, "has_custom_normals") and me.has_custom_normals:
    bpy.ops.object.select_all(action='DESELECT')
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.mesh.customdata_custom_splitnormals_clear()

# 4. CONFIGURAÇÃO DE SHAPE KEYS / MORPH TARGETS ORGÂNICOS
W = [M @ v.co for v in me.vertices]

JAW_PIVOT = Vector((0.0, -0.050, 3.420))
JAW_ANGLE = math.radians(11.0)
rot_jaw = (Matrix.Translation(JAW_PIVOT) @ Matrix.Rotation(JAW_ANGLE, 4, "X") @ Matrix.Translation(-JAW_PIVOT))

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

def smoothstep(e0, e1, x):
    t = min(max((x - e0) / (e1 - e0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)

if not me.shape_keys:
    mesh.shape_key_add(name="Basis", from_mix=False)

def add_key(name, displace):
    key = mesh.shape_key_add(name=name, from_mix=False)
    for i, p in enumerate(W):
        q = displace(i, p)
        if q is not None:
            key.data[i].co = Mi @ q
    return key

# --- MANDÍBULA E ABERTURA ORAL EM AMÊNDOA (ISOLADA ESTRITAMENTE À CABEÇA) ---
def jaw(i, p):
    # Proteção absoluta: vértices abaixo do queixo (Z < 3.24) NUNCA se movem!
    # Isso impede 100% de retração/deformação no pescoço, tórax, abdômen e pernas.
    if p.z < 3.24:
        return None
        
    kind = interior_kinds.get(i)
    if kind == "upper":
        return None
    if kind == "lower":
        return rot_jaw @ p
    if kind == "tongue":
        return p.lerp(rot_jaw @ p, 0.70)
        
    # Apenas vértices da cabeça
    if i not in head_verts_updated:
        return None

    ax = abs(p.x)
    # Lábio inferior e queixo
    if i in lower_indices or (3.24 <= p.z < 3.32 and p.y < -0.12 and ax < 0.08):
        if p.z < 3.32:
            # Queixo: movimento suave que decai a ZERO na base do queixo (Z=3.24)
            w_chin = smoothstep(3.24, 3.30, p.z) * (1.0 - smoothstep(0.04, 0.08, ax))
            w = w_chin * 0.90
        else:
            # Lábio inferior: afunila a EXATAMENTE zero na comissura CORNER_X
            if ax >= CORNER_X:
                return None
            curve_factor = (1.0 - (ax / CORNER_X) ** 2) ** 1.2
            w_z = smoothstep(3.3565, 3.330, 3.3565 - p.z)
            w = curve_factor * max(0.85, w_z)
            
        q = p.lerp(rot_jaw @ p, min(w, 1.0))
        if i in lower_indices and p.y < -0.21 and ax < CORNER_X:
            tuck = (1.0 - (ax / CORNER_X) ** 2)
            q += Vector((0.0, 0.0028 * tuck, -0.0018 * tuck))
        return q
    return None

def lip_weight(p, rx=0.075, rz=0.030):
    if p.y > -0.19 or p.z < 3.26:
        return 0.0
    d = math.sqrt((p.x / rx) ** 2 + ((p.z - 3.3565) / rz) ** 2)
    return 1.0 - smoothstep(0.45, 1.0, d)

def pucker(i, p):
    if i in interior_kinds or p.z < 3.26:
        return None
    w = lip_weight(p, rx=0.065, rz=0.025)
    if not w:
        return None
    return Vector((p.x * (1.0 - 0.20 * w), p.y - 0.007 * w, p.z + (3.3565 - p.z) * 0.05 * w))

def wide(i, p):
    if i in interior_kinds or p.z < 3.26:
        return None
    w = lip_weight(p, rx=0.080, rz=0.026)
    if not w:
        return None
    stretch = 0.0035 * w * min(abs(p.x) / 0.062, 1.0)
    return Vector((p.x + math.copysign(stretch, p.x), p.y + 0.0015 * w, p.z))

def lower_lip_in(i, p):
    if i in interior_kinds or i not in lower_indices or p.z < 3.26:
        return None
    w = lip_weight(p, rx=0.065, rz=0.026)
    if not w:
        return None
    return Vector((p.x, p.y + 0.004 * w, p.z + 0.003 * w))

def lips_press(i, p):
    if i in interior_kinds or p.z < 3.24:
        return None
    w = lip_weight(p, rx=0.07, rz=0.026)
    chin_w = 0.0
    if 3.24 <= p.z < 3.32 and p.y < -0.21 and abs(p.x) < 0.045:
        chin_w = smoothstep(3.24, 3.29, p.z) * (1.0 - smoothstep(0.02, 0.045, abs(p.x)))
    if not w and not chin_w:
        return None
    dz = (0.0020 * w if i in lower_indices else -0.0012 * w) + 0.0012 * chin_w
    return Vector((p.x * (1.0 - 0.02 * w), p.y + 0.0015 * w + 0.0006 * chin_w, p.z + dz))

def upper_lip_up(i, p):
    if i in interior_kinds or i in lower_indices or p.z < 3.26:
        return None
    w = lip_weight(p, rx=0.065, rz=0.03)
    if not w:
        return None
    return Vector((p.x, p.y - 0.0010 * w, p.z + 0.0038 * w))

tongue_tip_y = min((W[i].y for i, k in interior_kinds.items() if k == "tongue"), default=0.0)
def tongue_up(i, p):
    if interior_kinds.get(i) != "tongue":
        return None
    w = smoothstep(tongue_tip_y + 0.12, tongue_tip_y + 0.01, p.y)
    return Vector((p.x, p.y + 0.004 * w, p.z + 0.018 * w))

UPPER_BLINK_ANGLE = math.radians(33.5)
LOWER_BLINK_ANGLE = math.radians(-8.5)

def blink(i, p):
    if i in lashes:
        center, r, which = lashes[i]
        u = (p.x - center.x) / (1.05 * r)
        w_x = math.cos(min(abs(u), 1.0) * math.pi * 0.5) ** 1.3
        ang = UPPER_BLINK_ANGLE * w_x if which == "upper" else LOWER_BLINK_ANGLE * w_x
        return rotate_about_x(p, center, ang)
    if i in eyeball_verts or i in interior_kinds or i in hair or p.y > -0.16 or p.z < 3.35:
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

GAZE_YAW_ANGLE = math.radians(22.0)
GAZE_PITCH_ANGLE = math.radians(16.0)

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

def brow_up(i, p):
    if i in eyeball_verts or i in interior_kinds or i in hair or p.y > -0.10 or p.z < 3.45:
        return None
        
    # Cílios superiores acompanham suavemente a abertura atenta do olhar
    if i in lashes:
        center_l, r_l, which_lash = lashes[i]
        if which_lash == "upper":
            u = (p.x - center_l.x) / (1.05 * r_l)
            w_x = math.cos(min(abs(u), 1.0) * math.pi * 0.5) ** 1.3
            return p + Vector((0.0, -0.0006 * w_x, 0.0022 * w_x))
        return None

    if not (3.48 < p.z < 3.68 and abs(p.x) < 0.12 and p.y < -0.17):
        return None
    ax = abs(p.x)
    # Arco superciliar completo visível logo abaixo da franja
    w_x = math.cos(min(max((ax - 0.042) / 0.070, 0.0), 1.0) * math.pi * 0.5) ** 1.2
    # Transição suave da dobra palpebral (3.49) até o topo do arco superciliar (3.62)
    w_z = smoothstep(3.48, 3.53, p.z) * (1.0 - smoothstep(3.58, 3.66, p.z))
    w = w_x * w_z
    if w <= 0.001:
        return None
    asym = 1.03 if p.x > 0 else 0.97
    return p + Vector((0.0, 0.0012 * w * asym, 0.0105 * w * asym))

def brow_down(i, p):
    if i in lashes or i in eyeball_verts or i in interior_kinds or i in hair or p.y > -0.10 or p.z < 3.45:
        return None
    if not (3.47 < p.z < 3.64 and abs(p.x) < 0.10 and p.y < -0.16):
        return None
    ax = abs(p.x)
    
    # Músculo corrugador supercilii:
    # No plano sagital mediano (ax = 0), a contração medial é EXATAMENTE zero, impedindo colisão/fenda
    w_x = math.sin(min(max(ax / 0.080, 0.0), 1.0) * math.pi) ** 1.2
    w_z = smoothstep(3.47, 3.52, p.z) * (1.0 - smoothstep(3.56, 3.63, p.z))
    w = w_x * w_z
    if w <= 0.001:
        return None
    disp_x = -math.copysign(0.0038 * w, p.x)
    disp_y = 0.0025 * w
    disp_z = -0.0070 * w
    return p + Vector((disp_x, disp_y, disp_z))

MOUTH_CORNER_X = 0.052
MOUTH_CORNER_Y = -0.210
MOUTH_CORNER_Z = 3.3565

def smile(i, p):
    if i in interior_kinds or i in lashes or i in eyeball_verts or i in hair or p.y > -0.10 or p.z < 3.26:
        return None
    ax = abs(p.x)
    
    d_corner = math.sqrt(((ax - MOUTH_CORNER_X) / 0.030) ** 2 +
                         ((p.y - MOUTH_CORNER_Y) / 0.028) ** 2 +
                         ((p.z - MOUTH_CORNER_Z) / 0.028) ** 2)
    w_corner = 1.0 - smoothstep(0.0, 1.0, d_corner) if p.y < -0.18 else 0.0

    cheek_pos = Vector((math.copysign(0.060, p.x), -0.208, 3.425))
    d_cheek = (p - cheek_pos).length
    w_cheek = 1.0 - smoothstep(0.0, 0.050, d_cheek) if p.y < -0.17 else 0.0

    w_upper = 0.0
    if i not in lower_indices and 3.354 <= p.z < 3.385 and p.y < -0.210 and ax < 0.045:
        smile_arc = math.cos(min(ax / 0.045, 1.0) * math.pi * 0.5) ** 1.2
        z_factor = 1.0 - smoothstep(3.354, 3.385, p.z)
        w_upper = smile_arc * z_factor

    w_lower = 0.0
    if i in lower_indices and 3.335 < p.z <= 3.3565 and p.y < -0.215 and ax < 0.045:
        w_lower = (1.0 - (ax / 0.045) ** 2) * smoothstep(3.335, 3.3565, p.z)

    if w_corner <= 0.001 and w_cheek <= 0.001 and w_upper <= 0.001 and w_lower <= 0.001:
        return None

    asym = 1.02 if p.x > 0 else 0.98
    disp_x = math.copysign((0.0050 * w_corner + 0.0012 * w_cheek) * asym, p.x)
    disp_y = (0.0025 * w_corner - 0.0022 * w_cheek + 0.0012 * w_upper - 0.0008 * w_lower) * asym
    disp_z = (0.0075 * w_corner + 0.0040 * w_cheek + 0.0052 * w_upper + 0.0010 * w_lower) * asym
    return p + Vector((disp_x, disp_y, disp_z))

def squint(i, p):
    if i in interior_kinds or i in eyeball_verts or i in hair or p.y > -0.12 or p.z < 3.35:
        return None
        
    center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
    dx = p.x - center.x
    dz = p.z - center.z
    
    # 1. Cílios (acompanham perfeitamente as pálpebras, sem descolamento ou fendas)
    if i in lashes:
        center_l, r_l, which_lash = lashes[i]
        u = (p.x - center_l.x) / (1.05 * r_l)
        w_x = math.cos(min(abs(u), 1.0) * math.pi * 0.5) ** 1.3
        if which_lash == "lower":
            # Cílio inferior sobe junto da pálpebra inferior
            return p + Vector((0.0, -0.0020 * w_x, 0.0052 * w_x))
        else:
            # Cílio superior suaviza sutilmente para baixo na fresta amendoada
            return p + Vector((0.0, 0.0008 * w_x, -0.0016 * w_x))

    # 2. Pálpebra inferior (AU7 - Lid Tightener / elevação e espessamento)
    if abs(dx) < 1.15 * r and -0.75 * r <= dz < 0.12 * r and p.y < -0.17:
        u = dx / (1.15 * r)
        w_x = math.cos(min(abs(u), 1.0) * math.pi * 0.5) ** 1.3
        w_z = (1.0 - smoothstep(0.10 * r, 0.70 * r, -dz)) if dz < 0 else (1.0 - smoothstep(0.0, 0.12 * r, dz))
        w = w_x * w_z
        if w > 0.001:
            disp_x = -math.copysign(0.0008 * w, p.x)
            return p + Vector((disp_x, -0.0024 * w, 0.0055 * w))
            
    # 3. Pálpebra superior (suavização descendente amendoada típica do sorriso ocular Duchenne)
    if abs(dx) < 1.10 * r and 0.10 * r <= dz < 0.65 * r and p.y < -0.17:
        u = dx / (1.10 * r)
        w_x = math.cos(min(abs(u), 1.0) * math.pi * 0.5) ** 1.3
        w_z = 1.0 - smoothstep(0.10 * r, 0.60 * r, dz)
        w = w_x * w_z
        if w > 0.001:
            return p + Vector((0.0, 0.0009 * w, -0.0018 * w))
            
    # 4. Canto lateral externo do olho / Pés de galinha (AU6 - Orbicularis oculi pars orbitalis)
    dist_canthus = math.sqrt(((abs(p.x) - (abs(center.x) + 0.95 * r)) / 0.024) ** 2 + ((dz) / 0.020) ** 2)
    if dist_canthus < 1.0 and p.y < -0.17:
        w_c = 1.0 - smoothstep(0.0, 1.0, dist_canthus)
        disp_x = -math.copysign(0.0018 * w_c, p.x)
        return p + Vector((disp_x, -0.0012 * w_c, 0.0026 * w_c))
        
    return None

def brow_inner_up(i, p):
    if i in eyeball_verts or i in interior_kinds or i in hair or p.y > -0.10 or p.z < 3.45:
        return None

    center, r, _ = min(eyes, key=lambda e: abs(e[0].x - p.x))
    dx = p.x - center.x

    # 1. Cílios superiores na porção medial acompanham a elevação palpebral medial
    if i in lashes:
        center_l, r_l, which_lash = lashes[i]
        if which_lash == "upper" and (center_l.x * (p.x - center_l.x) < 0): # lado medial nasal
            u = abs(p.x - center_l.x) / (1.05 * r_l)
            w_l = math.cos(min(u, 1.0) * math.pi * 0.5) ** 1.5
            return p + Vector((0.0, -0.0004 * w_l, 0.0018 * w_l))
        return None

    if not (3.46 < p.z < 3.66 and abs(p.x) < 0.070 and p.y < -0.16):
        return None

    ax = abs(p.x)
    # Porção medial do músculo frontal (AU1): pico medial entre ax=0.015 e 0.035
    w_x = math.exp(-((ax - 0.022) ** 2) / (2 * (0.022 ** 2)))
    # Altura vertical: elevação máxima no arco superciliar medial (3.52 a 3.59)
    w_z = smoothstep(3.46, 3.53, p.z) * (1.0 - smoothstep(3.58, 3.66, p.z))
    w = w_x * w_z
    if w <= 0.001:
        return None

    asym = 1.02 if p.x > 0 else 0.98
    # Deslocamento: elevação superior (+Z), leve tração medial (+X em direção ao centro) nula em ax=0
    disp_x = -math.copysign(0.0012 * w * smoothstep(0.0, 0.020, ax), p.x)
    disp_y = -0.0015 * w * asym
    disp_z = 0.0090 * w * asym
    return p + Vector((disp_x, disp_y, disp_z))

def mouth_frown(i, p):
    if i in interior_kinds or i in eyeball_verts or i in lashes or i in hair or p.y > -0.15 or p.z < 3.25:
        return None

    ax = abs(p.x)
    # Foco anatômico na comissura labial (MOUTH_CORNER_X = 0.052, MOUTH_CORNER_Y = -0.210, MOUTH_CORNER_Z = 3.3565)
    d_corner = math.sqrt(((ax - MOUTH_CORNER_X) / 0.032) ** 2 +
                         ((p.y - MOUTH_CORNER_Y) / 0.030) ** 2 +
                         ((p.z - MOUTH_CORNER_Z) / 0.026) ** 2)
    w_corner = 1.0 - smoothstep(0.0, 1.0, d_corner) if p.y < -0.18 else 0.0

    # Lábio inferior lateral (acompanha o rebaixamento da comissura)
    w_lower = 0.0
    if i in lower_indices and 3.330 < p.z <= 3.3565 and p.y < -0.210 and 0.020 < ax < 0.052:
        w_lower = smoothstep(0.020, 0.052, ax) * smoothstep(3.330, 3.3565, p.z)

    # Sulco lábio-mandibular (depressor anguli oris AU15)
    w_dao = 0.0
    if 3.26 < p.z < 3.34 and 0.030 < ax < 0.065 and p.y < -0.19:
        w_dao = math.sin(smoothstep(3.26, 3.34, p.z) * math.pi) * (1.0 - smoothstep(0.030, 0.065, ax))

    w = max(w_corner, w_lower * 0.75, w_dao * 0.50)
    if w <= 0.001:
        return None

    asym = 1.02 if p.x > 0 else 0.98
    # Depressão oblíqua: desce (-Z), retrai sutilmente no sulco (+Y) e abre ligeiramente lateral (+X)
    disp_x = math.copysign((0.0016 * w_corner) * asym, p.x)
    disp_y = (0.0022 * w_corner + 0.0012 * w_dao) * asym
    disp_z = (-0.0068 * w_corner - 0.0040 * w_lower - 0.0025 * w_dao) * asym
    return p + Vector((disp_x, disp_y, disp_z))

def nose_wrinkle(i, p):
    if i in interior_kinds or i in eyeball_verts or i in lashes or i in hair or p.y > -0.18 or p.z < 3.36:
        return None

    if not (3.37 < p.z < 3.52 and abs(p.x) < 0.048 and p.y < -0.20):
        return None

    ax = abs(p.x)
    # Dorso nasal central (Procerus / Nasalis transversa AU9)
    w_dorsum = math.exp(-(ax ** 2) / (2 * (0.015 ** 2))) * smoothstep(3.40, 3.46, p.z) * (1.0 - smoothstep(3.48, 3.52, p.z))

    # Asas nasais e sulco alar (Levator labii superioris alaeque nasi)
    w_ala = 0.0
    if 0.012 < ax < 0.042 and 3.37 < p.z < 3.44:
        w_ala = math.sin(smoothstep(0.012, 0.042, ax) * math.pi) * math.sin(smoothstep(3.37, 3.44, p.z) * math.pi)

    # Sulco infraorbital medial
    w_sulcus = 0.0
    if 0.018 < ax < 0.048 and 3.44 < p.z < 3.50:
        w_sulcus = (1.0 - smoothstep(0.018, 0.048, ax)) * smoothstep(3.44, 3.50, p.z)

    w = max(w_dorsum, w_ala * 0.85, w_sulcus * 0.65)
    if w <= 0.001:
        return None

    asym = 1.02 if p.x > 0 else 0.98
    # Enrugamento: elevação (+Z), dilatação alar (+X) e dobra da raiz (-Y)
    disp_x = math.copysign(0.0015 * w_ala * asym, p.x)
    disp_y = (-0.0020 * w_dorsum - 0.0008 * w_ala) * asym
    disp_z = (0.0048 * w_dorsum + 0.0038 * w_ala + 0.0025 * w_sulcus) * asym
    return p + Vector((disp_x, disp_y, disp_z))

def chin_raise(i, p):
    # Proteção estrita do corpo: Z >= 3.245
    if i in interior_kinds or i in eyeball_verts or i in lashes or i in hair or p.z < 3.245:
        return None

    ax = abs(p.x)
    if ax > 0.055 or p.y > -0.19:
        return None

    # Coxim do queixo (Mentalis AU17, Z entre 3.245 e 3.325)
    w_mentalis = 0.0
    if 3.245 <= p.z < 3.325 and ax < 0.045:
        w_mentalis = smoothstep(3.245, 3.285, p.z) * (1.0 - smoothstep(3.305, 3.325, p.z)) * (1.0 - (ax / 0.045) ** 2)

    # Lábio inferior sendo empurrado para cima pelo queixo
    w_lower_lip = 0.0
    if i in lower_indices and 3.325 <= p.z < 3.3565 and ax < 0.040 and p.y < -0.210:
        w_lower_lip = (1.0 - (ax / 0.040) ** 2) * smoothstep(3.325, 3.3565, p.z)

    if w_mentalis <= 0.001 and w_lower_lip <= 0.001:
        return None

    # Mentalis eleva o queixo (+Z), projeta-o ligeiramente anterior (-Y) e comprime a linha mediana
    disp_x = -math.copysign(0.0010 * w_mentalis * smoothstep(0.0, 0.025, ax), p.x)
    disp_y = (-0.0024 * w_mentalis - 0.0015 * w_lower_lip)
    disp_z = (0.0048 * w_mentalis + 0.0035 * w_lower_lip)
    return p + Vector((disp_x, disp_y, disp_z))

def cheek_compress(i, p):
    if i in interior_kinds or i in eyeball_verts or i in lashes or i in hair or p.y > -0.16 or p.z < 3.28:
        return None

    ax = abs(p.x)
    # Região do músculo bucinador e modíolo AU14 (ax entre 0.038 e 0.085, Z entre 3.31 e 3.44)
    if not (0.038 < ax < 0.085 and 3.31 < p.z < 3.44 and -0.23 < p.y < -0.17):
        return None

    d_bucc = math.sqrt(((ax - 0.060) / 0.024) ** 2 +
                       ((p.y - (-0.200)) / 0.025) ** 2 +
                       ((p.z - 3.365) / 0.032) ** 2)
    w_bucc = 1.0 - smoothstep(0.0, 1.0, d_bucc)

    d_mod = math.sqrt(((ax - MOUTH_CORNER_X) / 0.020) ** 2 +
                      ((p.y - MOUTH_CORNER_Y) / 0.020) ** 2 +
                      ((p.z - MOUTH_CORNER_Z) / 0.020) ** 2)
    w_mod = 1.0 - smoothstep(0.0, 1.0, d_mod)

    w = max(w_bucc, w_mod * 0.70)
    if w <= 0.001:
        return None

    asym = 1.02 if p.x > 0 else 0.98
    # Compressão do bucinador contra os molares: retração posterior (+Y), medialização das bochechas (-|X|)
    disp_x = -math.copysign((0.0032 * w_bucc + 0.0018 * w_mod) * asym, p.x)
    disp_y = (0.0030 * w_bucc + 0.0022 * w_mod) * asym
    disp_z = ((3.365 - p.z) * 0.10 * w) * asym
    return p + Vector((disp_x, disp_y, disp_z))

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
add_key("BrowInnerUp", brow_inner_up)
add_key("MouthFrown", mouth_frown)
add_key("NoseWrinkle", nose_wrinkle)
add_key("ChinRaise", chin_raise)
add_key("CheekCompress", cheek_compress)

# 5. EXPORTAR FBX FINAL
bpy.ops.export_scene.fbx(
    filepath=DST,
    use_selection=False,
    object_types={"ARMATURE", "MESH"},
    add_leaf_bones=False,
    bake_anim=False,
    use_mesh_modifiers=False,
    mesh_smooth_type="FACE",
)
print("FBX EXPORTADO COM SUCESSO:", DST)
