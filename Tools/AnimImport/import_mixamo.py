"""Import the Mixamo animations (animation-only FBX, Mixamo skeleton) onto the imported Y Bot skeleton.

Source: ~/Downloads/FPSRL_Imports/<Role>/*.fbx (FBX Binary, Without Skin, 30 fps). The Y Bot (Characters tab, T-pose)
was imported first by probe_import.py into Characters/Mixamo/YBot and gives the skeleton.
Output: /Game/MainProject/Contents/Characters/Mixamo/Anims/<Role>/<Name> (Mixamo skeleton; retargeted later).
Run: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>"""
import os
import unreal

ROOT = os.path.join(os.path.expanduser("~"), "Downloads", "FPSRL_Imports")
DEST = "/Game/MainProject/Contents/Characters/Mixamo/Anims"
SKELETON = unreal.load_asset("/Game/MainProject/Contents/Characters/Mixamo/YBot/Y_Bot_Skeleton")
if not SKELETON:
    raise RuntimeError("Y Bot skeleton missing: run probe_import.py first")

manager = unreal.InterchangeManager.get_interchange_manager_scripted()
imported = 0
for role in sorted(os.listdir(ROOT)):
    folder = os.path.join(ROOT, role)
    if not os.path.isdir(folder):
        continue
    for name in sorted(os.listdir(folder)):
        if not name.lower().endswith(".fbx"):
            continue
        pipeline = unreal.InterchangeGenericAssetsPipeline()
        common = pipeline.common_skeletal_meshes_and_animations_properties
        common.set_editor_property("skeleton", SKELETON)
        common.set_editor_property("import_only_animations", True)
        pipeline.animation_pipeline.set_editor_property("import_animations", True)

        params = unreal.ImportAssetParameters()
        params.is_automated = True
        params.replace_existing = True
        params.override_pipelines.append(unreal.SoftObjectPath(pipeline.get_path_name()))
        source = unreal.InterchangeManager.create_source_data(os.path.join(folder, name))
        manager.import_asset(DEST + "/" + role, source, params)
        imported += 1

unreal.EditorAssetLibrary.save_directory(DEST)
for path in unreal.EditorAssetLibrary.list_assets(DEST, recursive=True):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.AnimSequence):
        unreal.log("[AnimImport] {} ({:.2f} s, skeleton {})".format(path.split(".")[0], asset.get_play_length(), asset.get_editor_property("skeleton").get_name()))
unreal.log("[AnimImport] done: {} file(s) imported".format(imported))
