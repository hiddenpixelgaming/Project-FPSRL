"""Miniboss #1 Ground Juggernaut (user spec 2026-10-05): a stationary war machine. Two shield layers broken by the
four-platform mechanic (UFPSRLShieldEncounterComponent), Grounded Missiles (GroundStrike), landmines (DeployMines).
Creates / updates: BP_Enemy_Juggernaut (child of the shooter enemy, its pistol hidden), DA_EnemyBehavior_Juggernaut,
DA_Enemy_Juggernaut (the Fab MPMECH mech as its placeholder body), DA_Encounter_GroundJuggernaut.
Run with the editor closed: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>
Then list DA_Enemy_Juggernaut in Config/DefaultGame.ini (EnemyDefinitions; not the role mix: it's a Miniboss)."""
import unreal

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
DATA = "/Game/MainProject/Contents/Data/Enemies"
ENCOUNTERS = "/Game/MainProject/Contents/Data/Encounters"
BLUEPRINTS = "/Game/MainProject/Contents/Blueprint/Enemies"
NPC = "/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"
PISTOL = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"
MECH = "/Game/MPMECH/MESHES/SK_MPMECH_LOD0"
MECH_ANIM = "/Game/MPMECH/BP/ABP_MPMECH.ABP_MPMECH_C"
Action = unreal.FPSRLEnemyAttackAction


def attack(**values):
    a = unreal.FPSRLEnemyAttack()
    for key, value in values.items():
        a.set_editor_property(key, value)
    return a


def copy(folder, source, name):
    path = folder + "/" + name
    if not lib.does_asset_exist(path):
        lib.duplicate_asset(folder + "/" + source, path)
    return unreal.load_asset(path)


# --- the pawn: the shooter enemy's Blueprint, its pistol hidden (bHideWeapon) ---------------------------------------
bp_path = BLUEPRINTS + "/BP_Enemy_Juggernaut"
if lib.does_asset_exist(bp_path):
    bp = unreal.load_asset(bp_path)
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.load_class(None, NPC))
    bp = tools.create_asset("BP_Enemy_Juggernaut", BLUEPRINTS, unreal.Blueprint, factory)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
cls = unreal.BlueprintEditorLibrary.generated_class(bp)
unreal.get_default_object(cls).set_editor_property("Weapon Class", unreal.load_class(None, PISTOL))
lib.save_loaded_asset(bp)

# --- behaviour: never moves, sees everyone, missiles + mines; the shield encounter's numbers --------------------------
profile = copy(DATA, "DA_EnemyBehavior_Shooter", "DA_EnemyBehavior_Juggernaut")
values = dict(
    movement_style=unreal.FPSRLMovementStyle.STATIONARY, preferred_min_distance=0.0, preferred_max_distance=9000.0,
    detection_range=9000.0, require_line_of_sight_to_detect=False, alerted_on_encounter_start=True, target_memory_seconds=999.0,
    proximity_aggro_radius=3000.0, damage_response=unreal.FPSRLDamageResponse.KEEP_ATTACKING,
    lost_sight_response=unreal.FPSRLLostSightResponse.REPOSITION, squad_movement=False, phase_on_projectile_hit=False,
    attacks=[
        # Landmines: every 8 s while a shield stands, 2 at a time, at most 8 down (it waits at the cap).
        attack(name="Mines", action=Action.DEPLOY_MINES, priority=20, min_range=0.0, max_range=9000.0, max_angle=180.0,
               requires_line_of_sight=False, cooldown=8.0, windup_seconds=0.3, execute_seconds=0.1, recovery_seconds=0.3,
               hold_position=True, only_while_shielded=True, mines_per_deploy=2, max_active_mines=8,
               mine_trigger_radius=150.0, mine_explosion_radius=300.0, mine_damage=40.0, mine_arm_seconds=1.5,
               mine_placement_radius=1900.0),
        # Grounded Missiles: every 4 s a mark under every player, 1.2 s warning, 25 damage in 3 m.
        attack(name="Missiles", action=Action.GROUND_STRIKE, priority=10, min_range=0.0, max_range=9000.0, max_angle=180.0,
               requires_line_of_sight=False, cooldown=4.0, windup_seconds=0.4, execute_seconds=0.1, recovery_seconds=0.4,
               hold_position=True, strike_warning_seconds=1.2, strike_radius=300.0, strike_damage=25.0, strike_targets=0),
    ],
    shield_layers=2, shield_mechanic_interval=30.0, shield_charge_seconds=2.0, shield_disruption_seconds=5.0,
    shield_reposition_radius=450.0, shield_shockwave_damage=150.0, shield_shockwave_radius=1500.0,
    shield_shockwave_speed=1100.0, shield_shockwave_height=45.0,
    reference_behaviour="Ground Juggernaut (Miniboss #1, user spec 2026-10-05): a stationary war machine. Two shield "
        "layers: its health can't be hurt until both are broken from the arena's four platforms - every 30 s the "
        "standing players are pulled next to it, as many platforms as players light up, it charges 2 s and sends out "
        "a jumpable shockwave (150); once every standing player is on a lit platform, 5 s of Shield Disruption breaks "
        "a layer (stepping off pauses it). Grounded Missiles every 4 s (a mark under every player, 1.2 s, 25 in 3 m). "
        "Landmines every 8 s while shielded (2 at a time, up to 8; 40 in 3 m; they stay until stepped on).")
for key, value in values.items():
    profile.set_editor_property(key, value)
lib.save_loaded_asset(profile)

# --- definition: 3000 health, the mech body ---------------------------------------------------------------------------
definition = copy(DATA, "DA_Enemy_NormalShooter", "DA_Enemy_Juggernaut")
definition.set_editor_property("display_name", "Ground Juggernaut")
definition.set_editor_property("enemy_class", cls)
definition.set_editor_property("base_health", 3000.0)
definition.set_editor_property("base_damage_multiplier", 1.0)
definition.set_editor_property("behavior_profile", profile)
definition.set_editor_property("body_mesh", unreal.load_asset(MECH))
definition.set_editor_property("body_anim_class", unreal.load_class(None, MECH_ANIM))
definition.set_editor_property("scale", 2.0)           # its hit box: twice a soldier
definition.set_editor_property("body_scale", 0.5)      # the mech at its own size (about 3.7 m) inside that
definition.set_editor_property("walk_speed", 0.0)
definition.set_editor_property("hide_weapon", True)
definition.set_editor_property("anim_set", unreal.FPSRLEnemyAnimSet())
definition.set_editor_property("attack_anims", {})
definition.set_editor_property("encounter_component", unreal.FPSRLShieldEncounterComponent.static_class())
lib.save_loaded_asset(definition)

# --- the encounter: the Miniboss bar, no placeholder multipliers ------------------------------------------------------
encounter = copy(ENCOUNTERS, "DA_Encounter_PlaceholderShooterMiniboss", "DA_Encounter_GroundJuggernaut")
encounter.set_editor_property("display_name", "Ground Juggernaut")
encounter.set_editor_property("kind", unreal.FPSRLEncounterKind.MINIBOSS)
encounter.set_editor_property("enemy_class", cls)
encounter.set_editor_property("health_multiplier", 1.0)
encounter.set_editor_property("damage_multiplier", 1.0)
encounter.set_editor_property("size_multiplier", 1.0)
encounter.set_editor_property("show_health_bar", True)
lib.save_loaded_asset(encounter)
unreal.log("[EnemySetup] Ground Juggernaut: {} health {:.0f}, scale {}, body {}, {} attack(s), encounter {}".format(
    cls.get_name(), definition.get_editor_property("base_health"), definition.get_editor_property("scale"),
    definition.get_editor_property("body_mesh").get_name(), len(profile.get_editor_property("attacks")), encounter.get_name()))
