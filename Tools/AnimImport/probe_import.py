"""Probe: import the Mixamo Y Bot (skeleton + mesh) and print the Interchange pipeline settings used for animation imports.
Run: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>"""
import os
import unreal

ROOT = os.path.join(os.path.expanduser("~"), "Downloads", "FPSRL_Imports")
DEST = "/Game/MainProject/Contents/Characters/Mixamo"
lib = unreal.EditorAssetLibrary

# Clean the earlier probe folder.
if lib.does_directory_exist(DEST + "/Probe"):
    lib.delete_directory(DEST + "/Probe")

task = unreal.AssetImportTask()
task.filename = os.path.join(ROOT, "Y Bot.fbx")
task.destination_path = DEST + "/YBot"
task.automated = True
task.save = True
task.replace_existing = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
for path in lib.list_assets(DEST + "/YBot", recursive=True):
    asset = unreal.load_asset(path)
    unreal.log("[AnimImport] created {} ({})".format(path, asset.get_class().get_name() if asset else "?"))

pipeline = unreal.InterchangeGenericAssetsPipeline()
unreal.log("[AnimImport] pipeline attrs: {}".format([a for a in dir(pipeline) if not a.startswith("_")]))
for name in ("common_skeletal_meshes_and_animations_properties", "animation_pipeline", "skeletal_mesh_pipeline", "common_meshes_properties"):
    sub = getattr(pipeline, name, None)
    if sub is not None:
        unreal.log("[AnimImport] {}: {}".format(name, [a for a in dir(sub) if not a.startswith("_")]))
