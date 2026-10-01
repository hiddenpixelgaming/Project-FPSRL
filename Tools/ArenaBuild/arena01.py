import unreal, traceback

out = []
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
lib = unreal.EditorAssetLibrary

SRC = "/Game/MainProject/Contents/Rooms/Zone1/LV_Z1_Combat_01"
LEVELS = "/Game/MainProject/Contents/Rooms/Arenas/"
DATA = "/Game/MainProject/Contents/Data/Rooms/Arenas/"
NAME = "LV_Arena_01_SunkenPlaza"
DATA_NAME = "DA_Room_Arena01_SunkenPlaza"
EXIT_X = 4500.0

CUBE = unreal.load_asset("/Engine/BasicShapes/Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder")
# Greyscale only (user decision): floors light grey, walls mid grey, landmark / cover dark.
M_FLOOR = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray")
M_WALL = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray_02")
M_FEATURE = unreal.load_asset("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark")

count = {"mesh": 0}

def place(label, mesh, center, size, mat, folder, yaw=0.0, pitch=0.0, roll=0.0):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center), unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw))
    a.set_actor_label(label)
    smc = a.static_mesh_component
    smc.set_static_mesh(mesh)
    smc.set_material(0, mat)
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    a.set_folder_path("Arena/" + folder)
    count["mesh"] += 1
    return a

def box(label, x0, x1, y0, y1, z0, z1, mat, folder):
    return place(label, CUBE, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0), mat, folder)

def boxc(label, cx, cy, cz, sx, sy, sz, mat, folder, yaw=0.0, pitch=0.0, roll=0.0):
    return place(label, CUBE, (cx, cy, cz), (sx, sy, sz), mat, folder, yaw, pitch, roll)

def cyl(label, cx, cy, z0, diameter, height, mat, folder):
    return place(label, CYL, (cx, cy, z0 + height / 2), (diameter, diameter, height), mat, folder)

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

def spawn_point(zone, x, y, yaw):
    a = sub.spawn_actor_from_class(unreal.FPSRLEnemySpawnPoint, unreal.Vector(x, y, 100.0), unreal.Rotator(yaw=yaw))
    a.set_actor_label("Spawn_%s_%d" % (zone, len([p for p in spawns if p.get_editor_property("spawn_zone") == zone])))
    a.set_editor_property("spawn_zone", zone)
    a.set_folder_path("Arena/Spawns")
    spawns.append(a)
    return a

spawns = []
try:
    if lib.does_asset_exist(LEVELS + NAME):
        lib.delete_asset(LEVELS + NAME)
    lib.duplicate_asset(SRC, LEVELS + NAME)
    world = unreal.EditorLoadingAndSavingUtils.load_map(LEVELS + NAME)
    for a in sub.get_all_level_actors():
        if a.get_actor_label() == "Geometry_Combat01" or a.get_class().get_name() == "FPSRLEnemySpawnPoint":
            sub.destroy_actor(a)

    # ---------------------------------------------------------------- floors
    box("Vestibule_Floor", 50, 600, -400, 400, -40, 0, M_FLOOR, "Floor")
    box("Plaza_Floor", 1300, 3400, -1050, 1050, -160, -120, M_FLOOR, "Floor")
    box("Rim_West", 600, 1400, -1750, 1750, -160, 0, M_FLOOR, "Floor")
    box("Rim_East", 3300, 4100, -1750, 1750, -160, 0, M_FLOOR, "Floor")
    box("Rim_South", 1400, 3300, -1750, -950, -160, 0, M_FLOOR, "Floor")
    box("Rim_North", 1400, 3300, 950, 1750, -160, 0, M_FLOOR, "Floor")
    for i, (cx, cy) in enumerate(((1400, 950), (1400, -950), (3300, 950), (3300, -950))):
        boxc("Rim_Chamfer_%d" % i, cx, cy, -80, 566, 566, 160, M_FLOOR, "Floor", yaw=45)   # octagonal plaza edge
    box("ExitPassage_Floor", 4100, 4500, -350, 350, -40, 0, M_FLOOR, "Floor")
    for tag, x0, x1 in (("W", 1300, 1700), ("E", 3000, 3400)):
        box("Alcove_N%s_Floor" % tag, x0, x1, 1750, 2450, -40, 0, M_FLOOR, "Floor")
        box("Alcove_S%s_Floor" % tag, x0, x1, -2450, -1750, -40, 0, M_FLOOR, "Floor")

    # ---------------------------------------------------------------- stairs into the plaza (rim 0 -> plaza -120)
    for k in range(1, 4):
        top = -30.0 * k
        box("Stairs_W_%d" % k, 1400 + 50 * (k - 1), 1400 + 50 * k, -250, 250, -160, top, M_FLOOR, "Stairs")
        box("Stairs_E_%d" % k, 3300 - 50 * k, 3300 - 50 * (k - 1), -250, 250, -160, top, M_FLOOR, "Stairs")
        box("Stairs_N_%d" % k, 2100, 2600, 950 - 50 * k, 950 - 50 * (k - 1), -160, top, M_FLOOR, "Stairs")
        box("Stairs_S_%d" % k, 2100, 2600, -950 + 50 * (k - 1), -950 + 50 * k, -160, top, M_FLOOR, "Stairs")

    # ---------------------------------------------------------------- landmark: broken monolith on a dais
    cyl("Monolith_Dais", 2350, 0, -120, 640, 40, M_FEATURE, "Landmark")
    boxc("Monolith_Shaft", 2350, 0, 330, 240, 240, 820, M_FEATURE, "Landmark", yaw=12)
    boxc("Monolith_Crown", 2385, 25, 840, 290, 290, 220, M_FEATURE, "Landmark", yaw=33, pitch=9, roll=6)
    boxc("Monolith_Fragment", 2860, 40, -35, 430, 210, 170, M_FEATURE, "Landmark", yaw=68)
    light("Monolith_Light", 2350, 0, 1000, 25000.0, 3200.0)

    # ---------------------------------------------------------------- plaza columns (cover, broken line of sight)
    for i, (x, y, h) in enumerate(((2810, 460, 420), (1890, 460, 260), (1890, -460, 160), (2810, -460, 340))):
        cyl("Plaza_Column_%d" % i, x, y, -120, 180, h, M_FEATURE, "Cover")

    # ---------------------------------------------------------------- rim low walls (waist cover looking into the plaza)
    for i, (cx, cy, yaw) in enumerate(((1579, 771, 45), (3121, 771, -45), (1579, -771, -45), (3121, -771, 45))):
        boxc("Rim_Wall_Diag_%d" % i, cx, cy, 55, 560, 40, 110, M_WALL, "Cover", yaw=yaw)
    for i, (x0, x1) in enumerate(((1850, 2050), (2650, 2850))):
        box("Rim_Wall_N_%d" % i, x0, x1, 965, 1005, 0, 110, M_WALL, "Cover")
        box("Rim_Wall_S_%d" % i, x0, x1, -1005, -965, 0, 110, M_WALL, "Cover")
    for i, (y0, y1) in enumerate(((300, 550), (-550, -300))):
        box("Rim_Wall_W_%d" % i, 1345, 1385, y0, y1, 0, 110, M_WALL, "Cover")
        box("Rim_Wall_E_%d" % i, 3315, 3355, y0, y1, 0, 110, M_WALL, "Cover")

    # ---------------------------------------------------------------- outer walls with the entrance, exit and four arches
    H = 700
    box("Wall_W_N", 500, 600, 400, 1800, 0, H, M_WALL, "Walls")
    box("Wall_W_S", 500, 600, -1800, -400, 0, H, M_WALL, "Walls")
    box("Wall_W_Lintel", 500, 600, -400, 400, 500, H, M_WALL, "Walls")
    box("Wall_E_N", 4100, 4200, 350, 1800, 0, H, M_WALL, "Walls")
    box("Wall_E_S", 4100, 4200, -1800, -350, 0, H, M_WALL, "Walls")
    box("Wall_E_Lintel", 4100, 4200, -350, 350, 500, H, M_WALL, "Walls")
    for side, y0, y1 in (("N", 1750, 1850), ("S", -1850, -1750)):
        box("Wall_%s_1" % side, 500, 1300, y0, y1, 0, H, M_WALL, "Walls")
        box("Wall_%s_2" % side, 1700, 3000, y0, y1, 0, H, M_WALL, "Walls")
        box("Wall_%s_3" % side, 3400, 4200, y0, y1, 0, H, M_WALL, "Walls")
        box("Wall_%s_ArchW_Lintel" % side, 1300, 1700, y0, y1, 450, H, M_WALL, "Walls")
        box("Wall_%s_ArchE_Lintel" % side, 3000, 3400, y0, y1, 450, H, M_WALL, "Walls")
    # Spawn alcoves behind the arches (lit so the openings read from across the hall).
    for side, sign in (("N", 1), ("S", -1)):
        for tag, x0, x1 in (("W", 1300, 1700), ("E", 3000, 3400)):
            ya, yb = sorted((1850 * sign, 2450 * sign))
            yc, yd = sorted((2450 * sign, 2550 * sign))
            box("Alcove_%s%s_SideA" % (side, tag), x0 - 100, x0, ya, yb, 0, H, M_WALL, "Alcoves")
            box("Alcove_%s%s_SideB" % (side, tag), x1, x1 + 100, ya, yb, 0, H, M_WALL, "Alcoves")
            box("Alcove_%s%s_Back" % (side, tag), x0 - 100, x1 + 100, yc, yd, 0, H, M_WALL, "Alcoves")
            ye, yf = sorted((1800 * sign, 2550 * sign))
            box("Alcove_%s%s_Roof" % (side, tag), x0 - 100, x1 + 100, ye, yf, H, H + 60, M_WALL, "Alcoves")
            zone = ("North" if sign > 0 else "South") + ("West" if tag == "W" else "East")
            cx = (x0 + x1) / 2
            yaw = -90.0 if sign > 0 else 90.0
            spawn_point(zone, cx - 90, 2050 * sign, yaw)
            spawn_point(zone, cx + 90, 2050 * sign, yaw)
            spawn_point(zone, cx, 2250 * sign, yaw)
            light("Alcove_%s%s_Light" % (side, tag), cx, 2150 * sign, 520, 6000.0, 1100.0)

    # ---------------------------------------------------------------- silhouette: gate pylons and corner buttresses
    for sign in (1, -1):
        y0, y1 = (400, 620) if sign > 0 else (-620, -400)
        y0, y1 = (400, 620) if sign > 0 else (-620, -400)
        box("Pylon_Entrance_%d" % sign, 490, 710, y0, y1, 0, 1000, M_FEATURE, "Silhouette")
        y0, y1 = (350, 570) if sign > 0 else (-570, -350)
        box("Pylon_Exit_%d" % sign, 4040, 4260, y0, y1, 0, 1000, M_FEATURE, "Silhouette")
    for i, (x, y) in enumerate(((575, 1775), (575, -1775), (4125, 1775), (4125, -1775))):
        boxc("Buttress_%d" % i, x, y, 450, 300, 300, 900, M_WALL, "Silhouette")

    # ---------------------------------------------------------------- vestibule (safe arrival) and exit passage
    box("Vestibule_Wall_N", 50, 600, 400, 500, 0, H, M_WALL, "Walls")
    box("Vestibule_Wall_S", 50, 600, -500, -400, 0, H, M_WALL, "Walls")
    box("Exit_Wall_N", 4200, 4500, 350, 450, 0, H, M_WALL, "Walls")
    box("Exit_Wall_S", 4200, 4500, -450, -350, 0, H, M_WALL, "Walls")
    box("Exit_End_N", 4450, 4500, 150, 350, 0, H, M_WALL, "Walls")
    box("Exit_End_S", 4450, 4500, -350, -150, 0, H, M_WALL, "Walls")
    box("Exit_End_Lintel", 4450, 4500, -150, 150, 300, H, M_WALL, "Walls")

    # ---------------------------------------------------------------- room logic
    room = [a for a in sub.get_all_level_actors() if a.get_class().get_name() == "BP_Room_C"][0]
    room.set_actor_location(unreal.Vector(2350, 0, 350), False, False)
    room.set_actor_scale3d(unreal.Vector(1.6, 1.733, 1.4))   # RoomBounds 1500x1500x500 -> 2400x2600x700
    room.set_editor_property("spawn_points", spawns)
    room.set_editor_property("enemy_count", 6)
    room.set_editor_property("active_spawn_zones", 2)
    trigger = [a for a in sub.get_all_level_actors() if a.get_class().get_name() == "BP_TriggerBox_Base_C"][0]
    trigger.set_actor_location(unreal.Vector(700, 0, 200), False, False)
    trigger.set_actor_rotation(unreal.Rotator(yaw=90), False)
    trigger.set_actor_scale3d(unreal.Vector(1.4, 1.0, 2.0))   # across the vestibule mouth: combat starts on entering the hall
    door = [a for a in sub.get_all_level_actors() if a.get_class().get_name() == "BP_Door_C"][0]
    door.set_actor_location(unreal.Vector(EXIT_X - 25, 0, 0), False, False)
    connector = [a for a in sub.get_all_level_actors() if a.get_class().get_name() == "FPSRLRoomConnector"][0]
    connector.set_actor_location(unreal.Vector(EXIT_X, 0, 0), False, False)
    nav = sub.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(2300, 0, 250), unreal.Rotator())
    nav.set_actor_label("NavBounds_Arena")
    nav.set_actor_scale3d(unreal.Vector(24.5, 26.0, 6.0))

    unreal.EditorLoadingAndSavingUtils.save_map(world, LEVELS + NAME)
    out.append("level %s: %d meshes, %d spawn points in zones %s" % (NAME, count["mesh"], len(spawns),
        sorted(set(str(p.get_editor_property("spawn_zone")) for p in spawns))))

    # ---------------------------------------------------------------- room definition + Depth pools
    if not lib.does_asset_exist(DATA + DATA_NAME):
        lib.duplicate_asset("/Game/MainProject/Contents/Data/Rooms/Zone1/DA_Room_Z1_Combat_01", DATA + DATA_NAME)
    r = unreal.load_asset(DATA + DATA_NAME)
    r.set_editor_property("display_name", "Sunken Plaza")
    r.set_editor_property("room_type", unreal.RoomType.COMBAT)
    r.set_editor_property("arena_type", unreal.FPSRLArenaType.OPEN)
    r.set_editor_property("level", unreal.load_asset(LEVELS + NAME))
    t = unreal.Transform()
    t.translation = unreal.Vector(EXIT_X, 0, 0)
    r.set_editor_property("exit_transform", t)
    lib.save_loaded_asset(r, False)
    for i in (1, 2, 3):
        d = unreal.load_asset("/Game/MainProject/Contents/Data/Run/DA_Depth_A1_D%d" % i)
        pool = list(d.get_editor_property("combat_rooms"))
        if r not in pool:
            pool.append(r)
            d.set_editor_property("combat_rooms", pool)
            lib.save_loaded_asset(d, False)
        out.append("%s combat pool: %s" % (d.get_name(), [p.get_name() for p in d.get_editor_property("combat_rooms")]))
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
