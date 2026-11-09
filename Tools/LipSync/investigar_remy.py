"""Script investigativo para análise minuciosa das expressões faciais do paciente Remy.
Gera capturas macro de alta fidelidade em múltiplos ângulos clínicos e analisa
a biometria dos morph targets (FACS).
"""

import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC = argv[0] if len(argv) > 0 else "Tools/LipSync/SK_Remy_Visemas.fbx"
OUT_DIR = argv[1] if len(argv) > 1 else "Tools/LipSync/investigacao"

os.makedirs(OUT_DIR, exist_ok=True)

# 1. Carrega o modelo com morph targets
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=SRC)
mesh_obj = next((x for x in bpy.data.objects if x.type == "MESH"), None)
if not mesh_obj:
    print("ERRO: Mesh não encontrado no FBX!")
    sys.exit(1)

shape_keys = mesh_obj.data.shape_keys
if not shape_keys:
    print("ERRO: Modelo não possui shape keys!")
    sys.exit(1)

keys = shape_keys.key_blocks
key_names = [k.name for k in keys]
print(f"Shape keys detectadas ({len(key_names)}):", key_names)

# Medição biométrica dos deslocamentos máximos de cada morph target
print("\n--- BIOMETRIA DOS MORPH TARGETS (FACS) ---")
basis = keys["Basis"]
biometria = {}
for k in keys[1:]:
    max_d = 0.0
    dx_max = dy_max = dz_max = 0.0
    for v_b, v_k in zip(basis.data, k.data):
        d_vec = v_k.co - v_b.co
        d_len = d_vec.length
        if d_len > max_d:
            max_d = d_len
            dx_max = d_vec.x
            dy_max = d_vec.y
            dz_max = d_vec.z
    biometria[k.name] = (max_d * 1000.0, dx_max * 1000.0, dy_max * 1000.0, dz_max * 1000.0)
    print(f"  * {k.name:15}: Max={max_d*1000.0:6.2f} mm | dX={dx_max*1000.0:6.2f} mm | dY={dy_max*1000.0:6.2f} mm | dZ={dz_max*1000.0:6.2f} mm")

# 2. Configura cena e estúdio de renderização
scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.studio_light = "Default"
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = "BOTH"
scene.display.shading.cavity_ridge_factor = 1.2
scene.display.shading.cavity_valley_factor = 1.5
scene.render.resolution_x = 800
scene.render.resolution_y = 800
scene.render.image_settings.file_format = "PNG"

# Pontos de foco anatômico do Remy
head_center = Vector((0.0, -0.21, 3.44))
mouth_center = Vector((0.0, -0.24, 3.355))
eyes_center = Vector((0.0, -0.20, 3.505))

# Conjunto de Câmeras Investigativas
cameras = {
    # 1. Visão do Aluno VR (perspectiva realista de quem está em pé ao lado da maca)
    "aluno_vr": {
        "type": "PERSP",
        "lens": 85,
        "target": head_center,
        "location": head_center + Vector((0.28, -1.05, 0.22)),
    },
    # 2. Retrato Clínico Frontal (Avaliação de simetria facial completa)
    "retrato_frontal": {
        "type": "PERSP",
        "lens": 85,
        "target": head_center,
        "location": head_center + Vector((0.0, -1.10, 0.0)),
    },
    # 3. Macro dos Olhos (Detecção de pálpebras, esclera, olhar e corrugador)
    "macro_olhos": {
        "type": "ORTHO",
        "ortho_scale": 0.22,
        "target": eyes_center,
        "location": eyes_center + Vector((0.04, -0.80, 0.02)),
    },
    # 4. Macro da Cavidade Oral (Lábios, dentes, encaixe labiodental, língua)
    "macro_boca": {
        "type": "ORTHO",
        "ortho_scale": 0.22,
        "target": mouth_center,
        "location": mouth_center + Vector((0.0, -0.80, -0.01)),
    },
    # 5. Perfil Sagital Clínico (Projeção do queixo, bico dos lábios, oclusão)
    "perfil_clinico": {
        "type": "ORTHO",
        "ortho_scale": 0.34,
        "target": head_center,
        "location": head_center + Vector((0.90, -0.15, 0.0)),
    },
}

# Cria e vincula os objetos de câmera
cam_objects = {}
for name, cfg in cameras.items():
    cam_data = bpy.data.cameras.new(name)
    cam_data.type = cfg["type"]
    if cfg["type"] == "PERSP":
        cam_data.lens = cfg["lens"]
    else:
        cam_data.ortho_scale = cfg["ortho_scale"]
    
    cam_obj = bpy.data.objects.new(name, cam_data)
    scene.collection.objects.link(cam_obj)
    cam_obj.location = cfg["location"]
    
    dir_to_target = (cfg["target"] - cfg["location"]).normalized()
    cam_obj.rotation_euler = dir_to_target.to_track_quat("-Z", "Y").to_euler()
    cam_objects[name] = cam_obj

# Catálogo Investigativo de Estados de Expressão e Fala
# Baseado na calibração do UPatientVoiceComponent e UPatientLookAtComponent
estados = {
    # ESTADOS FUNDAMENTAIS
    "01_repouso_natural": {
        # Totalmente neutro, lábios 100% selados, expressão serena
    },
    "02_saudacao_acolhedora": {
        "Smile": 0.40,
        "BrowUp": 0.35,
        "Squint": 0.25,
    },
    "03_escuta_atenta_concentrado": {
        "BrowDown": 0.30,
        "Squint": 0.20,
        "BrowUp": 0.10,
    },
    "04_hesitacao_pensando": {
        "BrowUp": 0.35,
        "EyeLookUp": 0.55,
        "EyeLookLeft": 0.40,
        "MouthPucker": 0.15,
        "JawOpen": 0.10,
    },
    # ARTICULAÇÃO FONÉTICA CONVERSACIONAL (LIP SYNC REAL)
    "05_fala_vogal_A_aberta": {
        # Pronúncia de 'A' ("Boa tarde", "Naka", "Braço")
        "JawOpen": 0.85,
        "BrowUp": 0.25,
        "Squint": 0.15,
    },
    "06_fala_vogal_E_articulada": {
        # Pronúncia de 'E' ("Enfermagem", "Você")
        "JawOpen": 0.50,
        "MouthWide": 0.18,
        "UpperLipUp": 0.06,
        "BrowUp": 0.15,
    },
    "07_fala_vogal_O_arredondada": {
        # Pronúncia de 'O' ("Hospital", "Doutor")
        "JawOpen": 0.50,
        "MouthPucker": 0.40,
        "BrowUp": 0.15,
    },
    "08_fala_vogal_U_bico": {
        # Pronúncia de 'U' ("Muito", "Tubo")
        "JawOpen": 0.22,
        "MouthPucker": 0.70,
        "BrowUp": 0.10,
    },
    "09_fala_consoante_MBP_selada": {
        # Pronúncia de 'M', 'B', 'P' ("Paciente", "Bom", "Pressão")
        "LipsPress": 0.75,
        "JawOpen": 0.0,
        "BrowUp": 0.15,
    },
    "10_fala_consoante_FV_labiodental": {
        # Pronúncia de 'F', 'V' ("Febre", "Veia")
        "LowerLipIn": 0.70,
        "JawOpen": 0.14,
        "UpperLipUp": 0.10,
    },
    "11_fala_consoante_L_lingua": {
        # Pronúncia de 'L' ("Olá", "Lado")
        "JawOpen": 0.50,
        "TongueUp": 0.65,
        "BrowUp": 0.20,
    },
    "12_fala_enfase_pergunta": {
        # Ênfase interrogativa ("Vai doer?", "Posso me mexer?")
        "JawOpen": 0.70,
        "BrowUp": 0.60,
        "Squint": 0.35,
        "Smile": 0.10,
    },
    "13_fala_sorriso_agradecimento": {
        # Agradecimento caloroso ("Muito obrigado!")
        "JawOpen": 0.55,
        "Smile": 0.35,
        "Squint": 0.40,
        "BrowUp": 0.30,
    },
    "14_piscada_durante_fala": {
        # Micro-piscada fisiológica enquanto pronuncia uma frase
        "Blink": 0.90,
        "JawOpen": 0.45,
        "BrowUp": 0.20,
    },
    "15_olhar_aluno_esquerda": {
        # Rastreamento ocular acompanhando movimento do aluno para a esquerda
        "EyeLookLeft": 0.75,
        "BrowUp": 0.15,
        "Squint": 0.10,
    },
    "16_olhar_aluno_direita": {
        # Rastreamento ocular acompanhando movimento do aluno para a direita
        "EyeLookRight": 0.75,
        "BrowUp": 0.15,
        "Squint": 0.10,
    },
}

print(f"\nIniciando renderização de {len(estados)} estados em {len(cameras)} ângulos...")
total_renders = 0

for estado_id, morph_weights in estados.items():
    # 1. Aplica pesos
    for k in keys[1:]:
        k.value = morph_weights.get(k.name, 0.0)
    
    # 2. Renderiza em cada câmera
    for cam_name, cam_obj in cam_objects.items():
        scene.camera = cam_obj
        filename = f"{estado_id}_{cam_name}.png"
        filepath = os.path.join(OUT_DIR, filename)
        scene.render.filepath = filepath
        bpy.ops.render.render(write_still=True)
        total_renders += 1

# Restaura repouso
for k in keys[1:]:
    k.value = 0.0

print(f"\nRenderização concluída com sucesso! Total de imagens geradas: {total_renders}")
