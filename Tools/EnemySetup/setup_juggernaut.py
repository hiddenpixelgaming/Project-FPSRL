"""Miniboss #1 Ground Juggernaut (user spec 2026-10-05): a stationary war machine. Two shield layers broken by the
four-platform mechanic (UFPSRLShieldEncounterComponent), mines thrown out at every pull, a forced pull when nobody is in
its sight, and a 3-shot volley + heavy shot (VolleyHeavy).
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
BULLET = "/Game/Variant_Shooter/Blueprints/Pickups/Projectiles/BP_ShooterProjectile_Bullet.BP_ShooterProjectile_Bullet_C"
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

# Mines (playtests v0.1.47-v0.1.49): no longer laid on a timer - thrown out all at once in a tight ring round it at the start of
# every pull (the warning that the pull is coming): 16 per throw, 9-10 m out (beyond where the pull puts players; 16 need that much ring), 1 s in
# the air, armed 1.5 s after landing, 40 in 3 m. Its shockwave sets them off as it passes.
mines = unreal.FPSRLMineSettings()
for key, value in dict(mines_per_throw=16, max_active_mines=32, trigger_radius=150.0, explosion_radius=300.0, damage=40.0,
                       throw_seconds=1.0, arm_seconds=1.5, min_distance=900.0, max_distance=1000.0).items():
    mines.set_editor_property(key, value)

# --- behaviour: never moves, sees everyone, the volley; the shield encounter's numbers ------ --------------------------
profile = copy(DATA, "DA_EnemyBehavior_Shooter", "DA_EnemyBehavior_Juggernaut")
values = dict(
    movement_style=unreal.FPSRLMovementStyle.STATIONARY, preferred_min_distance=0.0, preferred_max_distance=9000.0,
    detection_range=9000.0, require_line_of_sight_to_detect=False, alerted_on_encounter_start=True, target_memory_seconds=999.0,
    proximity_aggro_radius=3000.0, damage_response=unreal.FPSRLDamageResponse.KEEP_ATTACKING,
    lost_sight_response=unreal.FPSRLLostSightResponse.REPOSITION, squad_movement=False, phase_on_projectile_hit=False,
    attacks=[
        # A basic dodgeable attack (playtest v0.1.47): 3 slow shots at its target, then the Marksman-style heavy shot
        # (the laser for 1 s, then a fast 50). Needs to see its target. The missiles are kept for another boss.
        attack(name="Volley", action=Action.VOLLEY_HEAVY, priority=10, min_range=0.0, max_range=9000.0, max_angle=25.0,
               requires_line_of_sight=True, cooldown=2.5, windup_seconds=0.4, execute_seconds=0.1, recovery_seconds=0.6,
               hold_position=True, volley_projectile=unreal.load_class(None, BULLET), volley_shots=3, volley_interval=0.35,
               volley_damage=15.0, volley_speed=0.35, heavy_aim_seconds=1.0, heavy_damage=50.0, heavy_speed=0.9),
    ],
    shield_thresholds=[0.75, 0.5, 0.25], shielded_damage_taken=0.25, exposed_damage_taken=1.5,
    shield_charge_seconds=2.0, shield_disruption_seconds=5.0,
    shield_reposition_radius=450.0, shield_shockwave_damage=150.0, shield_shockwave_radius=1500.0,
    shield_shockwave_speed=1100.0, shield_shockwave_height=45.0,
    shield_mines=mines, forced_pull_blind_seconds=4.0, forced_pull_cooldown=12.0, forced_pull_charge_seconds=1.6,
    reference_behaviour="Ground Juggernaut (Miniboss #1, user spec 2026-10-05, reworked after playtests v0.1.47-v0.1.49): a "
        "stationary war machine. Its shield is up from the start (it takes 25% damage). At 75%, 50% and 25% health (it "
        "can't be pushed past one early) the shield comes up, it throws a ring of 16 mines round it, pulls the standing "
        "players next to it, lights as many platforms as players, charges 2 s and sends out a jumpable shockwave (150) that "
        "sets off the mines; once every standing player is on a lit platform, 5 s of Shield Disruption breaks the shield "
        "(stepping off pauses it; it keeps shooting at them meanwhile) and it takes 150% damage until the next threshold. "
        "Nobody in its sight for 4 s: a forced pull - mines, pull, the shockwave after 1.6 s, no platforms (12 s "
        "cooldown). Its attack: 3 slow dodgeable shots (15), then a laser for 1 s and a heavy shot (50).",
)
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
