"""Build IK Rigs + IK Retargeters and retarget the enemy / downed animations.

Sources:
  - Mixamo animations imported by import_mixamo.py (Y Bot skeleton): Characters/Mixamo/Anims/<Role>/
  - The project's UE5 Mannequin animations: /Game/Characters/Mannequins/Anims/{Rifle,Unarmed,Death}
Targets (role casting, user 2026-10-03: the four Sci-Fi Troopers are the four enemies):
  Grunt = SciFITrooper-01, Brute = SciFITrooper-02, Skirmisher = Trooper Girl 01, Marksman = Trooper Girl 02;
  Downed (players) = the UE5 Mannequin.
Output: /Game/MainProject/Contents/Characters/Enemies/<Role>/Anims/ and Characters/Players/Downed/.
Run: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>"""
import os
import unreal

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
RIGS = "/Game/MainProject/Contents/Characters/Retarget"
OUT = "/Game/MainProject/Contents/Characters"

MESHES = {
    "Mixamo": "/Game/MainProject/Contents/Characters/Mixamo/YBot/Y_Bot",
    "Mannequin": "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
    "Grunt": "/Game/Sci-FI_Troopers_Collection/SciFITrooper-01/SkeletalMesh/SK_SciFITrooper-01",
    "Brute": "/Game/Sci-FI_Troopers_Collection/SciFITrooper-02/SkeletalMesh/SK_SciFiTrooperV2",
    "Skirmisher": "/Game/Sci-FI_Troopers_Collection/SciFITrooper_Girl_01/SkeletalMesh/SK_SciFiTrooperGirlV1",
    "Marksman": "/Game/Sci-FI_Troopers_Collection/SciFITrooper_Girl_02/SkeletalMesh/SK_SciFiTrooperGirlV2",
}
MIXAMO = "/Game/MainProject/Contents/Characters/Mixamo/Anims/"
MANNY = "/Game/Characters/Mannequins/Anims/"
# (source mesh key, source folders, target key, output folder)
JOBS = [
    ("Mixamo", [MIXAMO + "Brute"], "Brute", OUT + "/Enemies/Brute/Anims"),
    ("Mixamo", [MIXAMO + "Skirmisher"], "Skirmisher", OUT + "/Enemies/Skirmisher/Anims"),
    ("Mixamo", [MIXAMO + "Marksman"], "Marksman", OUT + "/Enemies/Marksman/Anims"),
    ("Mixamo", [MIXAMO + "Downed"], "Mannequin", OUT + "/Players/Downed"),
    # The roles' own attacks on the Mannequin: Brute and Skirmisher play them on the enemy's (hidden) Mannequin rig.
    ("Mixamo", [MIXAMO + "Brute", MIXAMO + "Skirmisher"], "Mannequin", OUT + "/Enemies/Mannequin"),
    ("Mannequin", [MANNY + "Rifle", MANNY + "Death"], "Grunt", OUT + "/Enemies/Grunt/Anims"),
    ("Mannequin", [MANNY + "Rifle", MANNY + "Death"], "Marksman", OUT + "/Enemies/Marksman/Anims"),
    ("Mannequin", [MANNY + "Unarmed", MANNY + "Death", MANNY + "Rifle/HitReact"], "Brute", OUT + "/Enemies/Brute/Anims"),
    ("Mannequin", [MANNY + "Unarmed", MANNY + "Death", MANNY + "Rifle/HitReact"], "Skirmisher", OUT + "/Enemies/Skirmisher/Anims"),
]


def load(path):
    asset = unreal.load_asset(path)
    if not asset:
        raise RuntimeError("missing " + path)
    return asset


def make_rig(key):
    path = "{}/IK_{}".format(RIGS, key)
    # Reuse an earlier run's rig (deleting and recreating one name in a session fails).
    rig = unreal.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset("IK_" + key, RIGS, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(load(MESHES[key]))
    c.apply_auto_generated_retarget_definition()
    unreal.log("[Retarget] IK rig {}: root {}, {} chains".format(key, c.get_retarget_root(), len(c.get_retarget_chains())))
    lib.save_loaded_asset(rig)
    return rig


def make_retargeter(src, tgt, rigs):
    name = "RTG_{}_To_{}".format(src, tgt)
    path = "{}/{}".format(RIGS, name)
    rtg = unreal.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, RIGS, unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    c.remove_all_ops()
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rigs[src])
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, rigs[tgt])
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, load(MESHES[src]))
    c.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, load(MESHES[tgt]))
    c.add_default_ops()
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.SOURCE, rigs[src])
    c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, rigs[tgt])
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Mixamo is in a T-pose, the mannequins in an A-pose: line the target's retarget pose up with the source.
    c.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    lib.save_loaded_asset(rtg)
    unreal.log("[Retarget] retargeter {}".format(name))
    return rtg


rigs = {key: make_rig(key) for key in MESHES}
retargeters = {}
registry = unreal.AssetRegistryHelpers.get_asset_registry()
total = 0
# FPSRL_RETARGET_ONLY=<output folder part>: run only the matching jobs (e.g. "Enemies/Mannequin").
ONLY = os.environ.get("FPSRL_RETARGET_ONLY", "")
for src, folders, tgt, out in JOBS:
    if ONLY and ONLY not in out:
        continue
    key = (src, tgt)
    if key not in retargeters:
        retargeters[key] = make_retargeter(src, tgt, rigs)
    assets = []
    for folder in folders:
        for data in registry.get_assets_by_path(folder, recursive=True):
            if data.asset_class_path.asset_name in ("AnimSequence", "BlendSpace", "BlendSpace1D", "AimOffsetBlendSpace", "AimOffsetBlendSpace1D", "AnimMontage"):
                assets.append(data)
    if not assets:
        unreal.log_warning("[Retarget] nothing in {}".format(folders))
        continue
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.set_editor_property("assets_to_retarget", assets)
    inputs.set_editor_property("source_mesh", load(MESHES[src]))
    inputs.set_editor_property("target_mesh", load(MESHES[tgt]))
    inputs.set_editor_property("ik_retarget_asset", retargeters[key])
    inputs.set_editor_property("suffix", "_" + tgt)
    inputs.set_editor_property("target_path", out)
    inputs.set_editor_property("use_source_path", False)
    inputs.set_editor_property("include_referenced_assets", True)
    inputs.set_editor_property("overwrite_existing_files", True)
    results = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    moved = len(results)
    total += moved
    unreal.log("[Retarget] {} -> {}: {} asset(s) into {}".format(src, tgt, moved, out))

lib.save_directory(OUT + "/Enemies")
lib.save_directory(OUT + "/Players")
unreal.log("[Retarget] done: {} retargeted asset(s)".format(total))
