"""Exporta SK_Remy da Unreal para FBX para ser processado pelo Blender."""
import os
import unreal

FOLDER = "/Game/RefAssets/Remy"
NAME = "SK_Remy"
OUT_PATH = os.path.abspath(os.path.join(os.path.dirname(__file__), "SK_Remy.fbx"))

mesh = unreal.load_asset(f"{FOLDER}/{NAME}")
if not mesh:
    # Se SK_Remy não existir, carrega SK_Remy_Visemas
    mesh = unreal.load_asset(f"{FOLDER}/SK_Remy_Visemas")

task = unreal.AssetExportTask()
task.object = mesh
task.filename = OUT_PATH
task.selected = False
task.prompt = False
task.automated = True
task.options = unreal.FbxExportOption()
task.options.set_editor_property("fbx_export_compatibility", unreal.FbxExportCompatibility.FBX_2020)
task.options.set_editor_property("export_morph_targets", False)

unreal.Exporter.run_asset_export_task(task)
print(f"EXPORTADO COM SUCESSO: {OUT_PATH} (Tamanho: {os.path.getsize(OUT_PATH)} bytes)")

