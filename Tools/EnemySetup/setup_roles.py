"""Brute, Skirmisher, Marksman (user 2026-10-03; Combat Room Atlas section 4). For each role: a Blueprint child of the
shooter enemy (BP_Enemy_<Role>), a behaviour profile (from the shooter's, then the role's changes) and an enemy
definition (body, animations, size, speed, health). Safe to run again: existing assets are updated.
Run with the editor closed: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>
Afterwards list the definitions in Config/DefaultGame.ini (EnemyDefinitions + RoleMix)."""
import unreal

lib = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
DATA = "/Game/MainProject/Contents/Data/Enemies"
BLUEPRINTS = "/Game/MainProject/Contents/Blueprint/Enemies"
NPC = "/Game/Variant_Shooter/Blueprints/AI/BP_ShooterNPC.BP_ShooterNPC_C"
RIFLE = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C"
UNARMED = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"
TROOPERS = "/Game/Sci-FI_Troopers_Collection/"
ANIMS = "/Game/MainProject/Contents/Characters/Enemies/Mannequin/"

shooter_definition = unreal.load_asset(DATA + "/DA_Enemy_NormalShooter")
shooter_health = shooter_definition.get_editor_property("base_health")
Move = unreal.FPSRLMovementStyle
Close = unreal.FPSRLTooCloseResponse
Lost = unreal.FPSRLLostSightResponse
Hurt = unreal.FPSRLDamageResponse
Action = unreal.FPSRLEnemyAttackAction


def attack(**values):
    a = unreal.FPSRLEnemyAttack()
    for key, value in values.items():
        a.set_editor_property(key, value)
    return a


ROLES = {
    "Brute": dict(
        weapon=None, health=3.5, damage=1.0, scale=1.2, speed=380.0, anim_class=UNARMED,
        body=TROOPERS + "SciFITrooper-02/SkeletalMesh/SK_SciFiTrooperV2",
        anims={"Slam": ("Standing_Melee_Attack_Downward_Mannequin", 0.45)},
        profile=dict(preferred_min_distance=0.0, preferred_max_distance=180.0, movement_style=Move.CHASE,
                     too_close_response=Close.HOLD_AND_ATTACK, lost_sight_response=Lost.SEEK_LAST_SEEN,
                     damage_response=Hurt.KEEP_ATTACKING, separation_radius=150.0, acceptance_radius=80.0,
                     proximity_aggro_radius=800.0, reaction_time=0.4, squad_movement=False,
                     attacks=[attack(name="Slam", action=Action.MELEE, priority=10, min_range=0.0, max_range=260.0,
                                     max_angle=35.0, requires_line_of_sight=True, cooldown=1.5, windup_seconds=0.9,
                                     execute_seconds=0.15, recovery_seconds=0.9, hold_position=True,
                                     interruptible=False, melee_damage=30.0, melee_radius=120.0, melee_max_targets=4)]),
        note="Brute: slow, tough melee. Chases its target and slams (0.9 s wind-up with the overhead swing, 30 damage "
             "in a wide arc, hits up to 4 players). Pushes players out of safe spots; loops are how you kite it."),
    "Skirmisher": dict(
        weapon=None, health=0.6, damage=1.0, scale=1.0, speed=680.0, anim_class=UNARMED,
        body=TROOPERS + "SciFITrooper_Girl_01/SkeletalMesh/SK_SciFiTrooperGirlV1",
        anims={"Stab": ("Stabbing_Mannequin", 0.4)},
        profile=dict(preferred_min_distance=120.0, preferred_max_distance=260.0, movement_style=Move.STRAFE,
                     reposition_interval=1.0, strafe_distance=350.0, too_close_response=Close.HOLD_AND_ATTACK,
                     lost_sight_response=Lost.SEEK_LAST_SEEN, damage_response=Hurt.REPOSITION,
                     separation_radius=180.0, proximity_aggro_radius=800.0, reaction_time=0.25, squad_movement=False,
                     attacks=[attack(name="Stab", action=Action.MELEE, priority=10, min_range=0.0, max_range=240.0,
                                     max_angle=45.0, requires_line_of_sight=True, cooldown=0.9, windup_seconds=0.4,
                                     execute_seconds=0.1, recovery_seconds=0.5, hold_position=True,
                                     interruptible=True, melee_damage=12.0, melee_radius=70.0, melee_max_targets=1)]),
        note="Skirmisher: fast, fragile flanker. Runs in faster than a player walks, side-steps around its target and "
             "stabs (0.4 s wind-up, 12 damage); dodges away when hit. Comes from the side or behind."),
    "Marksman": dict(
        weapon=RIFLE, health=0.8, damage=3.0, scale=1.0, speed=450.0, anim_class=None,
        body=TROOPERS + "SciFITrooper_Girl_02/SkeletalMesh/SK_SciFiTrooperGirlV2",
        anims={},
        profile=dict(preferred_min_distance=1200.0, preferred_max_distance=4000.0,
                     movement_style=Move.GUARD_POSITION, guard_radius=300.0, too_close_response=Close.REPOSITION,
                     lost_sight_response=Lost.REPOSITION, detection_range=5000.0, aim_spread_degrees=0.5,
                     aim_lag_seconds=0.1, projectile_speed_multiplier=0.55, squad_movement=False,
                     attacks=[attack(name="Snipe", action=Action.FIRE_WEAPON, priority=10, min_range=0.0,
                                     max_range=4500.0, max_angle=10.0, requires_line_of_sight=True, cooldown=3.5,
                                     windup_seconds=1.3, execute_seconds=0.05, recovery_seconds=0.7,
                                     hold_position=True, interruptible=False, show_aim_line=True)]),
        note="Marksman: holds a high-ground spot (the highest spawn points). A red laser to its target for 1.3 s "
             "(thicker in the last 0.3 s), then one heavy, faster shot (3x damage). Break line of sight or climb to it."),
}


def blueprint(role, weapon):
    name = "BP_Enemy_" + role
    path = BLUEPRINTS + "/" + name
    if lib.does_asset_exist(path):
        bp = unreal.load_asset(path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.load_class(None, NPC))
        bp = tools.create_asset(name, BLUEPRINTS, unreal.Blueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(unreal.BlueprintEditorLibrary.generated_class(bp))
    cdo.set_editor_property("Weapon Class", unreal.load_class(None, weapon) if weapon else None)
    lib.save_loaded_asset(bp)
    return unreal.BlueprintEditorLibrary.generated_class(bp)


def duplicate(source, name):
    path = DATA + "/" + name
    if not lib.does_asset_exist(path):
        lib.duplicate_asset(DATA + "/" + source, path)
    return unreal.load_asset(path)


for role, r in ROLES.items():
    cls = blueprint(role, r["weapon"])

    profile = duplicate("DA_EnemyBehavior_Shooter", "DA_EnemyBehavior_" + role)
    for key, value in r["profile"].items():
        profile.set_editor_property(key, value)
    profile.set_editor_property("reference_behaviour", r["note"])
    lib.save_loaded_asset(profile)

    definition = duplicate("DA_Enemy_NormalShooter", "DA_Enemy_" + role)
    definition.set_editor_property("display_name", role)
    definition.set_editor_property("enemy_class", cls)
    definition.set_editor_property("base_health", shooter_health * r["health"])
    definition.set_editor_property("base_damage_multiplier", r["damage"])
    definition.set_editor_property("behavior_profile", profile)
    definition.set_editor_property("body_mesh", unreal.load_asset(r["body"]))
    definition.set_editor_property("anim_class", unreal.load_class(None, r["anim_class"]) if r["anim_class"] else None)
    anims = {}
    for attack_name, (anim, share) in r["anims"].items():
        sequence = unreal.load_asset(ANIMS + anim)
        entry = unreal.FPSRLEnemyAttackAnim()
        entry.set_editor_property("animation", sequence)
        entry.set_editor_property("impact_time", sequence.get_play_length() * share)
        anims[attack_name] = entry
        unreal.log("[EnemySetup] {} {}: {} ({:.2f} s, impact {:.2f} s)".format(role, attack_name, anim, sequence.get_play_length(), entry.get_editor_property("impact_time")))
    definition.set_editor_property("attack_anims", anims)
    definition.set_editor_property("scale", r["scale"])
    definition.set_editor_property("walk_speed", r["speed"])
    lib.save_loaded_asset(definition)
    unreal.log("[EnemySetup] {}: {} health {:.0f}, speed {:.0f}, scale {:.2f}, {} attack(s)".format(
        role, cls.get_name(), definition.get_editor_property("base_health"), r["speed"], r["scale"], len(r["profile"]["attacks"])))
