"""Grunt (user 2026-10-03): the existing shooter (bad aim, slow projectiles) in the Sci-Fi Trooper 01 body, moving
as a squad. Sets the data on the existing assets. Run with the editor closed:
UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>"""
import unreal

lib = unreal.EditorAssetLibrary
DEFINITION = "/Game/MainProject/Contents/Data/Enemies/DA_Enemy_NormalShooter"
PROFILE = "/Game/MainProject/Contents/Data/Enemies/DA_EnemyBehavior_Shooter"
BODY = "/Game/Sci-FI_Troopers_Collection/SciFITrooper-01/SkeletalMesh/SK_SciFITrooper-01.SK_SciFITrooper-01"

definition = unreal.load_asset(DEFINITION)
definition.set_editor_property("display_name", "Grunt")
definition.set_editor_property("base_health", 120.0)	# a little less after playtest v0.1.43 (was 150)
definition.set_editor_property("body_mesh", unreal.load_asset(BODY))
lib.save_loaded_asset(definition)

profile = unreal.load_asset(PROFILE)
profile.set_editor_property("squad_movement", True)
profile.set_editor_property("max_squad_size", 4)
profile.set_editor_property("squad_spacing", 220.0)
profile.set_editor_property("squad_regroup_distance", 300.0)
note = profile.get_editor_property("reference_behaviour") or ""
if "Grunt" not in note:
    profile.set_editor_property("reference_behaviour", note + "\nGrunt (user 2026-10-03): huddles and moves with its squad; the leader moves as this profile says, the others keep loose spots around it and stop only to shoot. Bad aim and slow projectiles as above.")
lib.save_loaded_asset(profile)

unreal.log("[EnemySetup] Grunt: body {}, squad {} (size {}, spacing {}, regroup {})".format(
    definition.get_editor_property("body_mesh"), profile.get_editor_property("squad_movement"),
    profile.get_editor_property("max_squad_size"), profile.get_editor_property("squad_spacing"), profile.get_editor_property("squad_regroup_distance")))
