"""Shared helpers for building authored combat arenas headlessly (UnrealEditor-Cmd -run=pythonscript).

Arena contract (same as every room): entrance at the origin facing +X (a safe vestibule X 50..600, Y -400..400), the
hall beyond, an exit passage ending in the door + room connector at exit_x. The room logic (BP_Room_C, combat trigger,
door, connector) comes from LV_Z1_Combat_01, re-positioned. Greyscale only (user decision): floors light, architecture
mid grey, cover / landmarks dark. Engine BasicShapes Cube / Cylinder are centred, 100 units.

Flow rule (user): players must clearly understand they go from one end to the other. Every arena gets exit_gate() (a
monumental frame + beacon light above the exit, visible from the entrance) and floor runners leading to it; the
entrance side stays plain and unlit.
"""
import unreal

sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lib = unreal.EditorAssetLibrary

SRC = "/Game/MainProject/Contents/Rooms/Zone1/LV_Z1_Combat_01"
LEVELS = "/Game/MainProject/Contents/Rooms/Arenas/"
DATA = "/Game/MainProject/Contents/Data/Rooms/Arenas/"

CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder")
M_FLOOR = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray")
M_WALL = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_02")
M_FEATURE = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark")

state = {"mesh": 0, "spawns": [], "world": None}


def begin(level_name):
    """The arena level, emptied of everything this kit builds (a new copy of the room template the first time).

    Rebuilding never deletes the level (a room definition references it, so a delete fails silently and the old
    contents would stay underneath): it clears every actor the kit placed (folder "Arena/...", the nav bounds, spawn
    points) and keeps the room logic (room, trigger, door, connector), which finish() positions again."""
    if not lib.does_asset_exist(LEVELS + level_name):
        if not lib.duplicate_asset(SRC, LEVELS + level_name):
            raise RuntimeError("could not create " + level_name)
    state["world"] = unreal.EditorLoadingAndSavingUtils.load_map(LEVELS + level_name)
    state["mesh"] = 0
    state["spawns"] = []
    removed = 0
    for a in sub.get_all_level_actors():
        folder = str(a.get_folder_path())
        if (folder.startswith("Arena") or a.get_actor_label() in ("Geometry_Combat01", "NavBounds_Arena")
                or a.get_class().get_name() in ("FPSRLEnemySpawnPoint", "NavMeshBoundsVolume")):
            sub.destroy_actor(a)
            removed += 1
    state["removed"] = removed


def count_level(level_name):
    """Actors by class in the saved level (duplicate check)."""
    unreal.EditorLoadingAndSavingUtils.load_map(LEVELS + level_name)
    counts = {}
    for a in sub.get_all_level_actors():
        name = a.get_class().get_name()
        counts[name] = counts.get(name, 0) + 1
    return counts


def place(label, mesh, center, size, mat, folder, yaw=0.0, pitch=0.0, roll=0.0):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
    a.set_actor_label(label)
    smc = a.static_mesh_component
    smc.set_static_mesh(mesh)
    smc.set_material(0, mat)
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    a.set_folder_path("Arena/" + folder)
    state["mesh"] += 1
    return a


def box(label, x0, x1, y0, y1, z0, z1, mat, folder):
    return place(label, CUBE, ((x0 + x1) / 2.0, (y0 + y1) / 2.0, (z0 + z1) / 2.0), (x1 - x0, y1 - y0, z1 - z0), mat, folder)


def boxc(label, cx, cy, cz, sx, sy, sz, mat, folder, yaw=0.0, pitch=0.0, roll=0.0):
    return place(label, CUBE, (cx, cy, cz), (sx, sy, sz), mat, folder, yaw, pitch, roll)


def cyl(label, cx, cy, z0, diameter, height, mat, folder):
    return place(label, CYL, (cx, cy, z0 + height / 2.0), (diameter, diameter, height), mat, folder)


def light(label, x, y, z, intensity, radius):
    a = sub.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z), unreal.Rotator())
    a.set_actor_label(label)
    c = a.point_light_component
    c.set_mobility(unreal.ComponentMobility.MOVABLE)
    c.set_intensity(intensity)
    c.set_attenuation_radius(radius)
    c.set_light_color(unreal.LinearColor(1, 1, 1, 1))
    a.set_folder_path("Arena/Lights")
    return a


def spawn_point(zone, x, y, yaw, floor_z=0.0):
    a = sub.spawn_actor_from_class(unreal.FPSRLEnemySpawnPoint, unreal.Vector(x, y, floor_z + 100.0), unreal.Rotator(yaw=yaw))
    index = len([p for p in state["spawns"] if str(p.get_editor_property("spawn_zone")) == zone])
    a.set_actor_label("Spawn_%s_%d" % (zone, index))
    a.set_editor_property("spawn_zone", zone)
    a.set_folder_path("Arena/Spawns")
    state["spawns"].append(a)
    return a


def vestibule(height=700.0):
    """Safe arrival space at the entrance (the combat trigger sits at its mouth). Plain and unlit on purpose."""
    box("Vestibule_Floor", 50, 600, -400, 400, -40, 0, M_FLOOR, "Floor")
    box("Vestibule_Wall_N", 50, 600, 400, 500, 0, height, M_WALL, "Walls")
    box("Vestibule_Wall_S", 50, 600, -500, -400, 0, height, M_WALL, "Walls")


def exit_passage(start_x, exit_x, height=700.0, half_width=350.0):
    """Passage from the hall's far wall (start_x) to the exit door at exit_x (door opening 300 wide, 300 high)."""
    box("ExitPassage_Floor", start_x, exit_x, -half_width, half_width, -40, 0, M_FLOOR, "Floor")
    box("Exit_Wall_N", start_x + 100, exit_x, half_width, half_width + 100, 0, height, M_WALL, "Walls")
    box("Exit_Wall_S", start_x + 100, exit_x, -half_width - 100, -half_width, 0, height, M_WALL, "Walls")
    box("Exit_End_N", exit_x - 50, exit_x, 150, half_width, 0, height, M_WALL, "Walls")
    box("Exit_End_S", exit_x - 50, exit_x, -half_width, -150, 0, height, M_WALL, "Walls")
    box("Exit_End_Lintel", exit_x - 50, exit_x, -150, 150, 300, height, M_WALL, "Walls")


def exit_gate(wall_x, half_opening=350.0, height=1300.0):
    """Flow rule: a monumental frame around the exit opening in the hall's far wall (wall_x = the wall's hall face),
    taller than everything else, with a beacon light above and inside it, so the way forward reads from the entrance."""
    for sign in (1, -1):
        y0, y1 = sorted((sign * half_opening, sign * (half_opening + 260)))
        box("ExitGate_Pylon_%s" % ("N" if sign > 0 else "S"), wall_x - 60, wall_x + 160, y0, y1, 0, height, M_FEATURE, "ExitGate")
        y2, y3 = sorted((sign * (half_opening + 260), sign * (half_opening + 420)))
        box("ExitGate_Step_%s" % ("N" if sign > 0 else "S"), wall_x - 30, wall_x + 130, y2, y3, 0, height * 0.75, M_FEATURE, "ExitGate")
    box("ExitGate_Lintel", wall_x - 80, wall_x + 180, -half_opening - 260, half_opening + 260, height - 200, height, M_FEATURE, "ExitGate")
    box("ExitGate_Crown", wall_x - 40, wall_x + 140, -160, 160, height, height + 220, M_FEATURE, "ExitGate")
    light("ExitGate_Beacon", wall_x - 150, 0, height + 120, 40000.0, 2600.0)
    light("ExitGate_Passage", wall_x + 250, 0, 450, 12000.0, 900.0)


def runner(label, x0, x1, z=0.0, half_width=110.0):
    """Flow rule: a dark floor runner along the hall's axis toward the exit (2 cm high, walkable)."""
    return box(label, x0, x1, -half_width, half_width, z, z + 2, M_FEATURE, "Runners")


def finish(level_name, data_name, display_name, arena_type, exit_x, room_center, room_extent, nav_center, nav_extent,
           enemy_count, active_zones, trigger_x=700.0, depths=(1, 2, 3), min_depth=0, max_depth=0):
    """Room logic, nav bounds, save, room definition and Depth pools. Returns log lines."""
    out = []
    actors = sub.get_all_level_actors()
    room = [a for a in actors if a.get_class().get_name() == "BP_Room_C"][0]
    room.set_actor_location(unreal.Vector(*room_center), False, False)
    room.set_actor_scale3d(unreal.Vector(room_extent[0] / 1500.0, room_extent[1] / 1500.0, room_extent[2] / 500.0))
    room.set_editor_property("spawn_points", state["spawns"])
    room.set_editor_property("enemy_count", enemy_count)
    room.set_editor_property("active_spawn_zones", active_zones)
    trigger = [a for a in actors if a.get_class().get_name() == "BP_TriggerBox_Base_C"][0]
    trigger.set_actor_location(unreal.Vector(trigger_x, 0, 200), False, False)
    trigger.set_actor_rotation(unreal.Rotator(yaw=90), False)
    trigger.set_actor_scale3d(unreal.Vector(1.4, 1.0, 2.0))
    door = [a for a in actors if a.get_class().get_name() == "BP_Door_C"][0]
    door.set_actor_location(unreal.Vector(exit_x - 25, 0, 0), False, False)
    connector = [a for a in actors if a.get_class().get_name() == "FPSRLRoomConnector"][0]
    connector.set_actor_location(unreal.Vector(exit_x, 0, 0), False, False)
    nav = sub.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(*nav_center), unreal.Rotator())
    nav.set_actor_label("NavBounds_Arena")
    nav.set_actor_scale3d(unreal.Vector(nav_extent[0] / 100.0, nav_extent[1] / 100.0, nav_extent[2] / 100.0))
    unreal.EditorLoadingAndSavingUtils.save_map(state["world"], LEVELS + level_name)
    zones = sorted(set(str(p.get_editor_property("spawn_zone")) for p in state["spawns"]))
    out.append("level %s: %d meshes built (%d old actors cleared), %d spawn points in zones %s" % (level_name, state["mesh"], state.get("removed", 0), len(state["spawns"]), zones))

    if not lib.does_asset_exist(DATA + data_name):
        lib.duplicate_asset("/Game/MainProject/Contents/Data/Rooms/Zone1/DA_Room_Z1_Combat_01", DATA + data_name)
    r = unreal.load_asset(DATA + data_name)
    r.set_editor_property("display_name", display_name)
    r.set_editor_property("room_type", unreal.RoomType.COMBAT)
    r.set_editor_property("arena_type", arena_type)
    r.set_editor_property("min_depth", min_depth)
    r.set_editor_property("max_depth", max_depth)
    r.set_editor_property("level", unreal.load_asset(LEVELS + level_name))
    t = unreal.Transform()
    t.translation = unreal.Vector(exit_x, 0, 0)
    r.set_editor_property("exit_transform", t)
    lib.save_loaded_asset(r, False)
    for i in depths:
        d = unreal.load_asset("/Game/MainProject/Contents/Data/Run/DA_Depth_A1_D%d" % i)
        pool = list(d.get_editor_property("combat_rooms"))
        if r not in pool:
            pool.append(r)
            d.set_editor_property("combat_rooms", pool)
            lib.save_loaded_asset(d, False)
        out.append("%s combat pool: %s" % (d.get_name(), [p.get_name() for p in d.get_editor_property("combat_rooms")]))
    return out
