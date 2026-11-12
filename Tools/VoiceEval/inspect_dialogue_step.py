import unreal

def inspect_dialogue_step():
    asset_path = "/Game/Blueprints/Stories/ProcedimentoStories/BP_Dialogue_Step"
    bp = unreal.load_asset(asset_path)
    print(f"Loaded asset: {bp} (Class: {bp.get_class().get_name() if bp else 'None'})")
    
    # Check generated class
    gen_class = unreal.load_class(None, "/Game/Blueprints/Stories/ProcedimentoStories/BP_Dialogue_Step.BP_Dialogue_Step_C")
    print(f"Generated class: {gen_class}")
    if gen_class:
        cdo = unreal.get_default_object(gen_class)
        print("CDO:", cdo)
        for prop in dir(cdo):
            if not prop.startswith('_'):
                try:
                    val = getattr(cdo, prop)
                    print(f"  {prop} = {val}")
                except:
                    pass

if __name__ == "__main__":
    inspect_dialogue_step()

