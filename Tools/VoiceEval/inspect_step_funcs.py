import unreal

def inspect():
    asset = unreal.load_asset("/Game/Blueprints/Stories/ProcedimentoStories/BP_Dialogue_Step")
    gen_class = unreal.load_class(None, "/Game/Blueprints/Stories/ProcedimentoStories/BP_Dialogue_Step.BP_Dialogue_Step_C")
    print(f"Asset: {asset}, GenClass: {gen_class}")
    
    # List functions
    if gen_class:
        funcs = [x for x in dir(gen_class) if not x.startswith('_')]
        print("Functions on BP_Dialogue_Step_C:")
        for f in funcs:
            print("  ", f)

if __name__ == "__main__":
    inspect()

