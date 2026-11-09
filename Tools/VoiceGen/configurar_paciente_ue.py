"""Configura a voz do paciente no projeto (rodar dentro da Unreal, com o editor fechado):

    UnrealEditor-Cmd.exe MedicalGestureLib.uproject -run=pythonscript -script="<caminho>/configurar_paciente_ue.py"

Requer o plugin Python Editor Script Plugin habilitado. O que faz:
  1. Importa output/*.wav para /Game/Audio/Paciente
  2. Importa output/DT_FalasPaciente.csv como DataTable (linha FPatientVoiceLine)
  3. No BP_Patient: troca a malha "remy" por SK_Remy_Visemas, adiciona o componente PatientVoice
     apontando para a DataTable e o PatientLookAt (paciente olha para o estudante)
Pode ser rodado de novo: reimporta os arquivos e reaproveita os componentes existentes.
"""

import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(HERE, "output")
AUDIO_PATH = "/Game/Audio/Paciente"
TABLE_NAME = "DT_FalasPaciente"
PATIENT_BP = "/Game/Blueprints/Patient/BP_Patient"
MESH_COMPONENT = "remy"
VISEME_MESH = "/Game/RefAssets/Remy/SK_Remy_Visemas"
VOICE_COMPONENT = "PatientVoice"
LOOK_COMPONENT = "PatientLookAt"


def log(msg):
    unreal.log_warning(f"PACIENTE: {msg}")


def import_task(filename, factory=None, name=""):
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = AUDIO_PATH
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = True
    if factory:
        task.factory = factory
    return task


def import_audio_and_table():
    wavs = sorted(f for f in os.listdir(OUTPUT) if f.endswith(".wav"))
    tasks = [import_task(os.path.join(OUTPUT, f)) for f in wavs]

    factory = unreal.CSVImportFactory()
    settings = factory.get_editor_property("automated_import_settings")
    settings.set_editor_property("import_type", unreal.CSVImportType.ECSV_DATA_TABLE)
    settings.set_editor_property("import_row_struct", unreal.PatientVoiceLine.static_struct())
    tasks.append(import_task(os.path.join(OUTPUT, TABLE_NAME + ".csv"), factory, TABLE_NAME))

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    log(f"importados {len(wavs)} wav + {TABLE_NAME}")

    table = unreal.load_asset(f"{AUDIO_PATH}/{TABLE_NAME}")
    rows = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
    log(f"DataTable com {len(rows)} linhas")
    return table


def find_subobject(subsystem, handles, name):
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for handle in handles:
        data = lib.get_data(handle)
        if str(lib.get_variable_name(data)) == name:
            return handle, lib.get_object(data)
    return None, None


def ensure_component(subsystem, bp, handles, name, cls):
    _, component = find_subobject(subsystem, handles, name)
    if component is None:
        params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=cls, blueprint_context=bp)
        handle, fail = subsystem.add_new_subobject(params)
        if not fail.is_empty():
            raise RuntimeError(f"não foi possível adicionar o componente {name}: {fail}")
        subsystem.rename_subobject(handle, unreal.Text(name))
        component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(
            unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle))
        log(f"componente {name} adicionado")
    return component


def setup_patient(table):
    bp = unreal.load_asset(PATIENT_BP)
    mesh = unreal.load_asset(VISEME_MESH)
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)

    _, mesh_comp = find_subobject(subsystem, handles, MESH_COMPONENT)
    old = mesh_comp.get_editor_property("skeletal_mesh_asset")
    mesh_comp.set_editor_property("skeletal_mesh_asset", mesh)
    log(f"malha do '{MESH_COMPONENT}': {old.get_name() if old else None} -> {mesh.get_name()}")

    old_skel = old.get_editor_property("skeleton") if old else None
    new_skel = mesh.get_editor_property("skeleton")
    log(f"esqueleto antigo={old_skel.get_path_name() if old_skel else None} novo={new_skel.get_path_name()}")
    log(f"morph targets: {[str(n) for n in mesh.get_all_morph_target_names()]}")

    voice = ensure_component(subsystem, bp, handles, VOICE_COMPONENT, unreal.PatientVoiceComponent)
    voice.set_editor_property("voice_lines", table)
    ensure_component(subsystem, bp, handles, LOOK_COMPONENT, unreal.PatientLookAtComponent)

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
    log("BP_Patient compilado e salvo")


setup_patient(import_audio_and_table())
log("OK")
