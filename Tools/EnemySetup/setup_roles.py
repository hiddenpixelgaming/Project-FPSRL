"""Brute, Skirmisher, Marksman (user 2026-10-03; Combat Room Atlas section 4; behaviours reworked after playtest v0.1.41). For each role: a Blueprint child of the
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
PISTOL = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"
UNARMED = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C"
TROOPERS = "/Game/Sci-FI_Troopers_Collection/"
ANIMS = "/Game/MainProject/Contents/Characters/Enemies/Mannequin/"

shooter_definition = unreal.load_asset(DATA + "/DA_Enemy_NormalShooter")
# Role health is a multiple of this (the Grunt's health when the roles were tuned; the Grunt itself is now 120).
shooter_health = 150.0
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


MANNY = "/Game/Characters/Mannequins/Anims/Unarmed/"
IDLE = MANNY + "MM_Idle"

ROLES = {
    "Brute": dict(
        weapon=PISTOL, hide_weapon=True, health=7.0, damage=2.0, scale=1.2, speed=520.0, anim_class=None,
        body=TROOPERS + "SciFITrooper-02/SkeletalMesh/SK_SciFiTrooperV2",
        # Locomotion: (animation, the speed it was authored for). Mixamo / Mannequin travel measured from the root.
        anim_set=dict(idle=IDLE, walk=(MANNY + "Walk/MF_Unarmed_Walk_Fwd", 300.0), run=(MANNY + "Jog/MF_Unarmed_Jog_Fwd", 600.0)),
        # Actions: (animation, start, impact, end) in seconds; StartTime..ImpactTime is timed to the wind-up / leap.
        anims={"Slam": (ANIMS + "Standing_Melee_Attack_Downward_Mannequin", 0.0, 1.02, 1.75),
               "LeapSlam": (ANIMS + "Standing_Taunt_Battlecry_Mannequin", 0.0, 1.9, 2.4),			# the roar
               "LeapSlam_Execute": (ANIMS + "Standing_Melee_Run_Jump_Attack_Mannequin", 0.75, 1.72, 2.6)},	# take-off .. landing
        profile=dict(preferred_min_distance=0.0, preferred_max_distance=180.0, movement_style=Move.CHASE,
                     too_close_response=Close.HOLD_AND_ATTACK, lost_sight_response=Lost.SEEK_LAST_SEEN,
                     damage_response=Hurt.KEEP_ATTACKING, separation_radius=150.0, acceptance_radius=80.0,
                     proximity_aggro_radius=800.0, reaction_time=0.4, squad_movement=False, phase_on_projectile_hit=False,
                     attacks=[attack(name="LeapSlam", action=Action.LEAP_SLAM, priority=20, min_range=500.0, max_range=2500.0,
                                     max_angle=30.0, requires_line_of_sight=True, cooldown=9.0, windup_seconds=1.6,
                                     execute_seconds=0.0, recovery_seconds=1.0, hold_position=True, interruptible=False,
                                     target_nearest_after=True, leap_seconds=0.9, shockwave_damage=50.0,
                                     shockwave_radius=1800.0, shockwave_speed=1400.0, shockwave_height=45.0),
                              attack(name="Slam", action=Action.MELEE, priority=10, min_range=0.0, max_range=320.0,
                                     max_angle=35.0, requires_line_of_sight=True, cooldown=1.5, windup_seconds=0.9,
                                     execute_seconds=0.15, recovery_seconds=0.9, hold_position=True,
                                     interruptible=False, melee_damage=30.0, melee_radius=200.0, melee_max_targets=4)]),
        note="Brute (after playtest v0.1.42): roars at its target (1.6 s), leaps to where the target stands (0.9 s in the air) and lands "
             "with a red shockwave ring along the ground (50 x2 = 100 damage (playtest v0.1.46), 18 m at 14 m/s; jump over it to take nothing). Pauses "
             "1 s, then rushes the nearest player with heavy overhead swings (0.9 s wind-up, 30 x2 damage, 2 m wide sweep, "
             "3.2 m reach). Leaps again every 9 s when its target is 5-25 m away. Health 7x a Grunt's."),
    "Skirmisher": dict(
        weapon=PISTOL, hide_weapon=True, health=130.0 / 150.0, damage=2.0, scale=1.0, speed=900.0, anim_class=None,
        body=TROOPERS + "SciFITrooper_Girl_01/SkeletalMesh/SK_SciFiTrooperGirlV1",
        anim_set=dict(idle=IDLE, walk=(MANNY + "Walk/MF_Unarmed_Walk_Fwd", 300.0), run=(ANIMS + "Sprint_Mannequin", 596.0)),
        anims={"Stab": (ANIMS + "Stabbing_Mannequin", 0.0, 0.85, 1.6),
               "Phase": (ANIMS + "Jump_Mannequin", 0.04, 0.62, 0.72)},	# jumps backwards
        profile=dict(preferred_min_distance=0.0, preferred_max_distance=150.0, movement_style=Move.CHASE,
                     too_close_response=Close.HOLD_AND_ATTACK, lost_sight_response=Lost.SEEK_LAST_SEEN,
                     damage_response=Hurt.KEEP_ATTACKING, separation_radius=180.0, proximity_aggro_radius=800.0,
                     reaction_time=0.25, squad_movement=False, phase_on_projectile_hit=True, phase_seconds=4.0,
                     phase_cooldown=3.0, phase_leap_distance=450.0, phase_leap_seconds=0.22,
                     attacks=[attack(name="Stab", action=Action.MELEE, priority=10, min_range=0.0, max_range=250.0,
                                     max_angle=45.0, requires_line_of_sight=True, cooldown=0.9, windup_seconds=0.5,
                                     execute_seconds=0.1, recovery_seconds=0.5, hold_position=False,
                                     interruptible=True, melee_damage=25.0, melee_radius=100.0, melee_max_targets=1)]),
        note="Skirmisher (after playtest v0.1.42): sprints straight at its target (9 m/s) and stabs on the run (keeps chasing through the 0.5 s wind-up, 25 x2 = 50 damage (playtest v0.1.44), 2.5 m reach; health 130) - it stood still to stab and moving players were never hit (playtest v0.1.43). "
             "Shot by a player: leaps back away from the shooter (0.22 s) and phases for 4 s - bullets pass through it and do "
             "nothing, melee still hurts - then can't phase again for 3 s."),
    "Marksman": dict(
        weapon=RIFLE, hide_weapon=False, health=1.2, damage=4.5, scale=1.0, speed=450.0, anim_class=None,
        body=TROOPERS + "SciFITrooper_Girl_02/SkeletalMesh/SK_SciFiTrooperGirlV2",
        anim_set=None,
        anims={},
        profile=dict(preferred_min_distance=1200.0, preferred_max_distance=4000.0,
                     movement_style=Move.GUARD_POSITION, guard_radius=300.0, too_close_response=Close.REPOSITION,
                     lost_sight_response=Lost.REPOSITION, detection_range=5000.0, aim_spread_degrees=0.5,
                     aim_lag_seconds=0.1, projectile_speed_multiplier=0.55, squad_movement=False, phase_on_projectile_hit=False,
                     attacks=[attack(name="Snipe", action=Action.FIRE_WEAPON, priority=10, min_range=0.0,
                                     max_range=4500.0, max_angle=10.0, requires_line_of_sight=True, cooldown=3.5,
                                     windup_seconds=1.3, execute_seconds=0.05, recovery_seconds=0.7,
                                     hold_position=True, interruptible=False, show_aim_line=True)]),
        note="Marksman: holds a high-ground spot (the highest spawn points). A red laser to its target for 1.3 s "
             "(thicker in the last 0.3 s), then one heavy, faster shot (4.5x damage after playtest v0.1.43; 1.2x Grunt health). Break line of sight or climb to it."),
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
    for action, (anim, start, impact, end) in r["anims"].items():
        sequence = unreal.load_asset(anim)
        entry = unreal.FPSRLEnemyAttackAnim()
        entry.set_editor_property("animation", sequence)
        entry.set_editor_property("start_time", start)
        entry.set_editor_property("impact_time", impact)
        entry.set_editor_property("end_time", end)
        anims[action] = entry
        unreal.log("[EnemySetup] {} {}: {} ({:.2f} s long, {:.2f}..{:.2f}..{:.2f})".format(role, action, anim.split("/")[-1], sequence.get_play_length(), start, impact, end))
    definition.set_editor_property("attack_anims", anims)
    anim_set = unreal.FPSRLEnemyAnimSet()
    if r["anim_set"]:
        anim_set.set_editor_property("idle", unreal.load_asset(r["anim_set"]["idle"]))
        anim_set.set_editor_property("walk", unreal.load_asset(r["anim_set"]["walk"][0]))
        anim_set.set_editor_property("walk_anim_speed", r["anim_set"]["walk"][1])
        anim_set.set_editor_property("run", unreal.load_asset(r["anim_set"]["run"][0]))
        anim_set.set_editor_property("run_anim_speed", r["anim_set"]["run"][1])
    definition.set_editor_property("anim_set", anim_set)
    definition.set_editor_property("scale", r["scale"])
    definition.set_editor_property("walk_speed", r["speed"])
    definition.set_editor_property("hide_weapon", r["hide_weapon"])
    lib.save_loaded_asset(definition)
    unreal.log("[EnemySetup] {}: {} health {:.0f}, speed {:.0f}, scale {:.2f}, {} attack(s)".format(
        role, cls.get_name(), definition.get_editor_property("base_health"), r["speed"], r["scale"], len(r["profile"]["attacks"])))
