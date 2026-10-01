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
    state["gate"] = (wall_x, half_opening, height)
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


def name_plate(display_name):
    """User request (2026-10-01): the arena's name on the wall the players face as they enter. It goes across the face of
    the exit gate's lintel (exit_gate() must run first): the one thing everyone looks at from the entrance, high enough
    to read over the fight. Dark unlit letters on the gate."""
    wall_x, half_opening, height = state["gate"]
    text = sub.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(wall_x - 86, 0, height - 100), unreal.Rotator(yaw=180.0))
    text.set_actor_label("NamePlate_Text")
    comp = text.text_render
    comp.set_text(display_name.upper())
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property("vertical_alignment", unreal.VerticalTextAligment.EVRTA_TEXT_CENTER)
    comp.set_editor_property("world_size", 170.0)
    comp.set_editor_property("text_render_color", unreal.Color(r=8, g=8, b=8, a=255))
    # Unlit (a copy of the engine text material with its colour on emissive): lit text washed out under the beacon.
    comp.set_material(0, unreal.load_asset("/Game/MainProject/Contents/Materials/Arena/M_ArenaNameText"))
    text.set_folder_path("Arena/Label")
    return text


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
    name_plate(display_name)
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


def hazard_material():
    """Hazard surfaces are ORANGE (user, 2026-10-01: the one colour exception to the greyscale arenas, so players read
    "this hurts" at a glance). The grid material colours top faces with its Top* parameters, so both sets are applied,
    on every build so tweaks here take effect."""
    path = "/Game/MainProject/Contents/Materials/Arena/MI_Arena_HazardPool"
    if lib.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset("MI_Arena_HazardPool", "/Game/MainProject/Contents/Materials/Arena",
                                                                     unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(mi, unreal.load_asset("/Game/LevelPrototyping/Materials/M_PrototypeGrid"))
    surface, grid, sub_grid = (1.0, 0.28, 0.02), (0.45, 0.08, 0.0), (0.75, 0.18, 0.01)
    for name, c in (("SurfaceColor", surface), ("GridColor", grid), ("SubGridColor", sub_grid),
                    ("TopSurfaceColor", surface), ("TopGridColor", grid), ("TopSubGridGridColor", sub_grid)):
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, name, unreal.LinearColor(c[0], c[1], c[2], 1))
    lib.save_loaded_asset(mi, False)
    return mi


def hazard_zone(label, x0, x1, y0, y1, surface_z, damage_per_second=10.0):
    """Hazard logic box just under walking height: a pawn standing ON the hazard surface (capsule bottom at surface_z)
    overlaps it; one standing on stone 40 cm higher does not. Enemies' navigation avoids its footprint at that height."""
    a = sub.spawn_actor_from_class(unreal.FPSRLHazardZone, unreal.Vector((x0 + x1) / 2.0, (y0 + y1) / 2.0, surface_z - 2.0), unreal.Rotator())
    a.set_actor_label(label)
    a.get_editor_property("zone").set_box_extent(unreal.Vector((x1 - x0) / 2.0, (y1 - y0) / 2.0, 27.0))
    a.set_editor_property("damage_per_second", damage_per_second)
    a.set_folder_path("Arena/Hazards")
    return a




def jump_pad(label, x, y, z, vx, vy, vz):
    """A jump pad (AFPSRLJumpPad, the BP_JumpPad look: launches a PLAYER who steps on it with Velocity, world space;
    rooms are chained without rotation; enemies are never launched and path around it). Player gravity 980: apex height = vz^2 / 1960, so vz 940 ~ 4.5 m, 1100 ~ 6.2 m; flight across a gap at the
    same height lasts 2 * vz / 980 s. Enemies never use them (no nav link): every level a pad reaches must also have stairs
    or a ramp. User limit: at most 2-3 per room."""
    a = sub.spawn_actor_from_class(unreal.FPSRLJumpPad, unreal.Vector(x, y, z), unreal.Rotator())
    a.set_actor_label(label)
    a.set_editor_property("velocity", unreal.Vector(vx, vy, vz))
    a.set_folder_path("Arena/JumpPads")
    return a


def ramp(label, x0, x1, y0, y1, z0, z1, mat, folder, thick=40.0):
    """A sloped slab whose top surface rises from z0 at x0 to z1 at x1 (walkable below ~44 degrees: enemies use it)."""
    import math
    dx, dz = x1 - x0, z1 - z0
    length = math.hypot(dx, dz)
    pitch = math.degrees(math.atan2(dz, dx))
    nx, nz = -math.sin(math.radians(pitch)), math.cos(math.radians(pitch))   # the slab's up direction in the XZ plane
    cx, cz = (x0 + x1) / 2.0 - nx * thick / 2.0, (z0 + z1) / 2.0 - nz * thick / 2.0
    return boxc(label, cx, (y0 + y1) / 2.0, cz, length, y1 - y0, thick, mat, folder, pitch=pitch)


def shell(hall_end, half_y, height, side_doors=(), far_doors=True, entry_lintel=500.0):
    """The hall's outer boundary: entrance wall (opening Y -400..400 up to entry_lintel), side walls with door gaps,
    exit wall (exit opening Y -350..350, far doors at Y +-900..1300 when far_doors). side_doors: (x0, x1, sign, sill_z,
    lintel_z) gaps in the north (+1) / south (-1) wall; sill_z > 0 for an upper door (wall below it)."""
    box("Wall_W_N", 500, 600, 400, half_y + 100, 0, height, M_WALL, "Walls")
    box("Wall_W_S", 500, 600, -half_y - 100, -400, 0, height, M_WALL, "Walls")
    box("Wall_W_Lintel", 500, 600, -400, 400, entry_lintel, height, M_WALL, "Walls")
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        a, b = sorted((half_y * sign, (half_y + 100) * sign))
        x = 500
        for i, (x0, x1, s, sill, lintel) in enumerate(sorted(d for d in side_doors if d[2] == sign)):
            box("Wall_%s_%d" % (tag, i), x, x0, a, b, 0, height, M_WALL, "Walls")
            if sill > 0:
                box("Wall_%s_%d_Sill" % (tag, i), x0, x1, a, b, 0, sill, M_WALL, "Walls")
            box("Wall_%s_%d_Lintel" % (tag, i), x0, x1, a, b, lintel, height, M_WALL, "Walls")
            x = x1
        box("Wall_%s_End" % tag, x, hall_end + 100, a, b, 0, height, M_WALL, "Walls")
        segments = ((350, 900, 0), (900, 1300, 450), (1300, half_y, 0)) if far_doors else ((350, half_y, 0),)
        for y0, y1, z0 in segments:
            c, d = sorted((y0 * sign, y1 * sign))
            box("Wall_E_%s_%d" % (tag, y0), hall_end, hall_end + 100, c, d, z0, height, M_WALL, "Walls")
        if far_doors:
            far_door(tag, sign, hall_end, height)


def far_door(tag, sign, hall_end, height):
    """A spawn room through the exit wall beside the exit (zone FarDoors), facing back into the hall."""
    a3, b3 = sorted((900 * sign, 1300 * sign))
    box("FarDoor_%s_Floor" % tag, hall_end, hall_end + 700, a3, b3, -40, 0, M_FLOOR, "Alcoves")
    a4, b4 = sorted((800 * sign, 900 * sign))
    box("FarDoor_%s_SideA" % tag, hall_end + 100, hall_end + 700, a4, b4, 0, height, M_WALL, "Alcoves")
    a5, b5 = sorted((1300 * sign, 1400 * sign))
    box("FarDoor_%s_SideB" % tag, hall_end + 100, hall_end + 700, a5, b5, 0, height, M_WALL, "Alcoves")
    a6, b6 = sorted((800 * sign, 1400 * sign))
    box("FarDoor_%s_Back" % tag, hall_end + 700, hall_end + 800, a6, b6, 0, height, M_WALL, "Alcoves")
    box("FarDoor_%s_Roof" % tag, hall_end + 50, hall_end + 800, a6, b6, 450, 510, M_WALL, "Alcoves")
    spawn_point("FarDoors", hall_end + 350, 1020 * sign, 180.0)
    spawn_point("FarDoors", hall_end + 350, 1180 * sign, 180.0)
    light("FarDoor_%s_Light" % tag, hall_end + 400, 1100 * sign, 380, 6000.0, 1000.0)


def side_room(zone, x0, x1, sign, half_y, height, floor_z=0.0, depth=650.0):
    """A spawn room behind the north (+1) / south (-1) wall, open toward the hall across X x0..x1 (match a shell
    side_door), floor at floor_z (an upper door: floor_z > 0), 3 spawn points facing into the hall."""
    tag = "%s_%s" % (zone, "N" if sign > 0 else "S")
    w0, w1 = half_y, half_y + 100
    a, b = sorted((w0 * sign, (w1 + depth) * sign))
    box("%s_Floor" % tag, x0, x1, a, b, floor_z - 40, floor_z, M_FLOOR, "Alcoves")
    c, d = sorted((w1 * sign, (w1 + depth) * sign))
    box("%s_SideA" % tag, x0 - 100, x0, c, d, 0, height, M_WALL, "Alcoves")
    box("%s_SideB" % tag, x1, x1 + 100, c, d, 0, height, M_WALL, "Alcoves")
    e, f = sorted(((w1 + depth) * sign, (w1 + depth + 100) * sign))
    box("%s_Back" % tag, x0 - 100, x1 + 100, e, f, 0, height, M_WALL, "Alcoves")
    g, h = sorted(((w0 + 50) * sign, (w1 + depth + 100) * sign))
    box("%s_Roof" % tag, x0 - 100, x1 + 100, g, h, floor_z + 450, floor_z + 510, M_WALL, "Alcoves")
    cx = (x0 + x1) / 2.0
    yaw = -90.0 if sign > 0 else 90.0
    for fx, fy in ((-90, 0.35), (90, 0.35), (0, 0.7)):
        spawn_point(zone, cx + fx, (w1 + depth * fy) * sign, yaw, floor_z=floor_z)
    light("%s_Light" % tag, cx, (w1 + depth * 0.55) * sign, floor_z + 380, 6000.0, 1000.0)


def stairs(label, x, y, z_from, z_to, direction, width, mat, folder, step_h=25.0, step_d=30.0):
    """Straight stairs starting at (x, y) on z_from and climbing to z_to, running along direction '+x', '-x', '+y' or
    '-y'; width across. Solid steps (enemies walk them). Returns the far end coordinate along the run."""
    count = max(1, int(round(abs(z_to - z_from) / step_h)))
    rise = (z_to - z_from) / count
    axis, sign = direction[1], (1 if direction[0] == "+" else -1)
    for s in range(1, count + 1):
        a, b = sorted(((s - 1) * step_d * sign, s * step_d * sign))
        top = z_from + rise * s
        if axis == "x":
            box("%s_%d" % (label, s), x + a, x + b, y - width / 2.0, y + width / 2.0, min(z_from, z_to) - 40, top, mat, folder)
        else:
            box("%s_%d" % (label, s), x - width / 2.0, x + width / 2.0, y + a, y + b, min(z_from, z_to) - 40, top, mat, folder)
    return count * step_d   # the run length
