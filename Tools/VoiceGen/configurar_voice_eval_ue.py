"""Adiciona e configura o UPatientVoiceEvaluationComponent no BP_Patient:

    UnrealEditor-Cmd.exe MedicalGestureLib.uproject -run=pythonscript -script="Tools/VoiceGen/configurar_voice_eval_ue.py" -stdout -unattended -nosplash
"""

import os
import unreal

PATIENT_BP = "/Game/Blueprints/Patient/BP_Patient"
EVAL_COMPONENT = "PatientVoiceEvaluation"

def log(msg):
    unreal.log_warning(f"VOICE_EVAL: {msg}")

def find_subobject(subsystem, handles, name):
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    for handle in handles:
        data = lib.get_data(handle)
        if str(lib.get_variable_name(data)) == name:
            return handle, lib.get_object(data)
    return None, None

def ensure_component(subsystem, bp, handles, name, cls):
    handle, component = find_subobject(subsystem, handles, name)
    if component is None:
        params = unreal.AddNewSubobjectParams(parent_handle=handles[0], new_class=cls, blueprint_context=bp)
        handle, fail = subsystem.add_new_subobject(params)
        if not fail.is_empty():
            raise RuntimeError(f"não foi possível adicionar o componente {name}: {fail}")
        subsystem.rename_subobject(handle, unreal.Text(name))
        component = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(
            unreal.SubobjectDataBlueprintFunctionLibrary.get_data(handle))
        log(f"componente {name} adicionado com sucesso!")
    else:
        log(f"componente {name} já existia no BP_Patient.")
    return component

def setup_voice_evaluation():
    if not hasattr(unreal, "PatientVoiceEvaluationComponent"):
        log("ERRO: unreal.PatientVoiceEvaluationComponent não encontrado! Verifique a compilação do C++.")
        return False

    bp = unreal.load_asset(PATIENT_BP)
    if not bp:
        log(f"ERRO: Não foi possível carregar {PATIENT_BP}")
        return False

    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)

    eval_comp = ensure_component(subsystem, bp, handles, EVAL_COMPONENT, unreal.PatientVoiceEvaluationComponent)
    
    # Configurações padrão da API
    eval_comp.set_editor_property("base_url", "https://voice-eval-api.onrender.com")
    eval_comp.set_editor_property("api_key", "wcP90Sipv_00QbUM8O_NBDsALc76AJSX6bt1A2aXLtg")
    eval_comp.set_editor_property("scenario_id", "coleta-sangue")
    eval_comp.set_editor_property("auto_wake_up_server_on_start", True)
    eval_comp.set_editor_property("auto_advance_on_pass", True)
    eval_comp.set_editor_property("auto_trigger_patient_response", True)

    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False)
    log("BP_Patient configurado com Voice Evaluation, compilado e salvo com sucesso!")
    return True

if __name__ == "__main__":
    setup_voice_evaluation()
