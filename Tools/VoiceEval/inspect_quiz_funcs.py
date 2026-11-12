import unreal

def inspect():
    gen_class = unreal.load_class(None, "/Game/Blueprints/Interface/BP_Quiz.BP_Quiz_C")
    print(f"GenClass: {gen_class}")
    if gen_class:
        funcs = [x for x in dir(gen_class) if not x.startswith('_')]
        print("Functions on BP_Quiz_C:")
        for f in funcs:
            print("  ", f)

if __name__ == "__main__":
    inspect()

