# Enemy / downed animation import

The licensed art is **not in Git** (the repo is public; the Fab and Mixamo licences don't allow sharing the raw files).
These folders exist only on the dev PC and in packaged builds, and are listed in `.gitignore`:

- `Content/Sci-FI_Troopers_Collection/` (Fab: Sci-Fi Troopers Collection, added with "Add to Project")
- `Content/MainProject/Contents/Characters/Mixamo/` (Y Bot + imported Mixamo animations)
- `Content/MainProject/Contents/Characters/Enemies/` and `.../Players/Downed/` (retargeted animations)
- `Content/MainProject/Contents/Characters/Retarget/` (IK Rigs and IK Retargeters)

## Rebuild from scratch

1. **Fab:** Epic Games Launcher > Unreal Engine > Library > Fab Library > *Sci-Fi Troopers Collection* > Add to Project > FPSRL.
2. **Mixamo** (FBX Binary, 30 fps, keyframe reduction none, Without Skin, *In Place* where offered), into
   `~/Downloads/FPSRL_Imports/<Role>/`:
   - `Y Bot.fbx`: Characters tab > Y Bot > Download, Pose T-pose (top level of FPSRL_Imports)
   - `Brute/`: Mutant Walking, Standing Melee Attack Downward, Standing Melee Run Jump Attack, Standing Taunt Battlecry
   - `Skirmisher/`: Sprint, Stabbing, Jump
   - `Marksman/`: Idle Aiming, Firing Rifle
   - `Downed/`: Crawl Forward, Writhing In Pain, Falling Down, Standing Up
3. With the editor closed, run each script headlessly, in order:
   `UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<script> -unattended -nullrhi`
   1. `probe_import.py` imports Y Bot (the Mixamo skeleton).
   2. `import_mixamo.py` imports the animations onto it.
   3. `retarget_all.py` builds the IK Rigs / Retargeters and retargets: Mixamo -> each role's trooper and the player
      Mannequin (downed set); the project's Mannequin rifle / unarmed / death / hit-react sets -> the troopers.
      Also Mixamo Brute / Skirmisher -> the Mannequin (Characters/Enemies/Mannequin: the Brute's slam and the
      Skirmisher's stab play on the enemy's hidden Mannequin rig). FPSRL_RETARGET_ONLY=<folder part> runs only matching jobs.
4. Check by eye: `FPSRL.AnimReview` in a rendered game (one screenshot per role in Saved/Screenshots).

Role casting: Grunt = SciFITrooper-01, Brute = SciFITrooper-02, Skirmisher = Trooper Girl 01, Marksman = Trooper Girl 02.

## Enemy roles

After the animations: `Tools/EnemySetup/setup_grunt.py`, `setup_roles.py` (Brute / Skirmisher / Marksman Blueprints,
behaviour profiles and definitions) and `make_role_materials.py`, each headless like above. Check with
`FPSRL.RoleTest` and `FPSRL.RoleBehaviourTest` in the Lobby (headless; rendered runs also take screenshots), and
`FPSRL.EnemyMotionWatch` during a real run (movement needs a navmesh, which the Lobby has none of).
