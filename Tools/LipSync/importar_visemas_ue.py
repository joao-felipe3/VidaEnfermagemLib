"""Reimporta SK_Remy_Visemas.fbx (gerado por criar_visemas_remy.py) no projeto, com o editor fechado:

    UnrealEditor-Cmd.exe MedicalGestureLib.uproject -run=pythonscript -script="Tools/LipSync/importar_visemas_ue.py <fbx>"

Requer o plugin Python Editor Script Plugin habilitado. Mantém esqueleto, materiais e physics asset do
SK_Remy_Visemas atual e marca os materiais para uso com morph targets (senão a Unreal troca o material
do rosto por um padrão na primeira fala).
"""

import sys

import unreal

FBX = sys.argv[-1]
FOLDER = "/Game/RefAssets/Remy"
NAME = "SK_Remy_Visemas"


def log(msg):
    unreal.log_warning(f"VISEMAS: {msg}")


old = unreal.load_asset(f"{FOLDER}/{NAME}")
skeleton = old.get_editor_property("skeleton")
materials = [m.get_editor_property("material_interface") for m in old.get_editor_property("materials")]
physics = old.get_editor_property("physics_asset")

options = unreal.FbxImportUI()
options.set_editor_property("import_mesh", True)
options.set_editor_property("import_as_skeletal", True)
options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
options.set_editor_property("skeleton", skeleton)
options.set_editor_property("import_materials", False)
options.set_editor_property("import_textures", False)
options.set_editor_property("import_animations", False)
options.set_editor_property("create_physics_asset", False)
options.skeletal_mesh_import_data.set_editor_property("import_morph_targets", True)

task = unreal.AssetImportTask()
task.filename = FBX
task.destination_path = FOLDER
task.destination_name = NAME
task.automated = True
task.replace_existing = True
task.save = False
task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

mesh = unreal.load_asset(f"{FOLDER}/{NAME}")
slots = mesh.get_editor_property("materials")
for slot, material in zip(slots, materials):
    slot.set_editor_property("material_interface", material)
mesh.set_editor_property("materials", slots)
mesh.set_editor_property("physics_asset", physics)
unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)

for material in materials:
    base = material.get_base_material() if material else None
    if base and not base.get_editor_property("used_with_morph_targets"):
        base.set_editor_property("used_with_morph_targets", True)
        unreal.MaterialEditingLibrary.recompile_material(base)
        unreal.EditorAssetLibrary.save_loaded_asset(base, only_if_is_dirty=False)
        log(f"{base.get_name()}: Used with Morph Targets ligado")

log(f"esqueleto={mesh.get_editor_property('skeleton').get_name()} "
    f"morphs={[str(n) for n in mesh.get_all_morph_target_names()]}")
log("OK")
