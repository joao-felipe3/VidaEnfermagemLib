import sys
import unreal

FBX = sys.argv[-1] if len(sys.argv) > 1 and sys.argv[-1].endswith(".fbx") else "c:/Users/GILSON/Documents/NakaGames/VidaEnfermagemLib-main/Tools/LipSync/SK_Remy_Visemas.fbx"
FOLDER = "/Game/RefAssets/Remy"
NAME = "SK_Remy_Visemas"

def log(msg):
    unreal.log_warning(f"VISEMAS_IMPORT: {msg}")

skeleton = unreal.load_asset(f"{FOLDER}/SK_Remy_Skeleton")
remy_ref = unreal.load_asset(f"{FOLDER}/SK_Remy")

if not skeleton or not remy_ref:
    log("ERRO: Esqueleto ou SK_Remy não encontrados!")
    sys.exit(1)

materials = [m.get_editor_property("material_interface") for m in remy_ref.get_editor_property("materials")]
physics = remy_ref.get_editor_property("physics_asset")

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
task.save = True
task.options = options

log(f"Importando {FBX} para {FOLDER}/{NAME}...")
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

new_mesh = unreal.load_asset(f"{FOLDER}/{NAME}")
if not new_mesh:
    log("ERRO: Falha ao carregar novo asset importado!")
    sys.exit(1)

slots = new_mesh.get_editor_property("materials")
for slot, material in zip(slots, materials):
    slot.set_editor_property("material_interface", material)
new_mesh.set_editor_property("materials", slots)
new_mesh.set_editor_property("physics_asset", physics)

unreal.EditorAssetLibrary.save_loaded_asset(new_mesh, only_if_is_dirty=False)

for material in materials:
    base = material.get_base_material() if material else None
    if base and not base.get_editor_property("used_with_morph_targets"):
        base.set_editor_property("used_with_morph_targets", True)
        unreal.MaterialEditingLibrary.recompile_material(base)
        unreal.EditorAssetLibrary.save_loaded_asset(base, only_if_is_dirty=False)
        log(f"{base.get_name()}: Used with Morph Targets habilitado")

morph_names = [str(n) for n in new_mesh.get_all_morph_target_names()]
log(f"SUCESSO TOTAL! Esqueleto={new_mesh.get_editor_property('skeleton').get_name()}")
log(f"Morph targets ({len(morph_names)}): {morph_names}")
