"""The Ground Juggernaut's three arena layouts (user 2026-10-05: build all three, compare in play). Each: an entrance
vestibule, a hall with the boss in its middle (it never moves), exactly four permanent elevated platforms (2.5 m up,
7 x 7 m tops, a ramp each facing the boss, about 19-20 m from it: outside its 15 m shockwave) and the exit at the far end.

  A  Cross Court        - rebuilds the existing Miniboss level LV_Z1_Miniboss_01: platforms N / S / W / E around the
                          boss in a 50 x 46 m hall, a full open ring of floor between.
  B  Diagonal Bastions  - LV_Miniboss_02_DiagonalBastions: platforms on the four diagonals of a 48 x 48 m hall.
  C  Sunken Foundry     - LV_Miniboss_03_SunkenFoundry: the boss in a 16 x 16 m pit (1.5 m down, a ramp out of each side),
                          platforms against the outer walls at the compass points.

All three are Miniboss rooms of Depth 2 (picked at random); fpsrl.Depth.ForceMinibossRoom <data name> forces one.
Run with the editor closed: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, cyl, light, spawn_point, M_FLOOR, M_WALL, M_FEATURE

ZONE1_LEVELS = "/Game/MainProject/Contents/Rooms/Zone1/"
ZONE1_DATA = "/Game/MainProject/Contents/Data/Rooms/Zone1/"
ENCOUNTER = unreal.load_asset("/Game/MainProject/Contents/Data/Encounters/DA_Encounter_GroundJuggernaut")
H = 1000.0          # wall height (the mech is 3.7 m; the platform beams rise above the walls)
TOP = 250.0         # platform tops
HALF = 350.0        # platform half size (7 x 7 m)


def hall(x0, x1, half_y, floor=True):
    """Floor, side walls, the entrance wall (opening Y -400..400) and the exit wall (opening Y -350..350)."""
    if floor:
        box("Hall_Floor", x0, x1, -half_y, half_y, -40, 0, M_FLOOR, "Floor")
    box("Wall_W_N", x0 - 100, x0, 400, half_y + 100, 0, H, M_WALL, "Walls")
    box("Wall_W_S", x0 - 100, x0, -half_y - 100, -400, 0, H, M_WALL, "Walls")
    box("Wall_W_Lintel", x0 - 100, x0, -400, 400, 500, H, M_WALL, "Walls")
    for sign in (1, -1):
        a, b = sorted((half_y * sign, (half_y + 100) * sign))
        box("Wall_%s" % ("N" if sign > 0 else "S"), x0 - 100, x1 + 100, a, b, 0, H, M_WALL, "Walls")
        c, d = sorted((350 * sign, half_y * sign))
        box("Wall_E_%s" % ("N" if sign > 0 else "S"), x1, x1 + 100, c, d, 0, H, M_WALL, "Walls")
    box("Wall_E_Lintel", x1, x1 + 100, -350, 350, 450, H, M_WALL, "Walls")


def lights(x0, x1, half_y):
    """An even grid of fill lights over the hall."""
    step = 1200.0
    x = x0 + step / 2
    while x < x1:
        y = -half_y + step / 2
        while y < half_y:
            light("Fill_%d_%d" % (x, y), x, y, H - 150, 9000.0, 1800.0)
            y += step
        x += step


def boss(x, y, floor_z=0.0):
    """Where the Juggernaut stands (facing the entrance), with a low dais ring around it as its landmark."""
    spawn_point("Boss", x, y, 180.0, floor_z=floor_z)
    cyl("Boss_Dais", x, y, floor_z - 40, 900, 48, M_FEATURE, "Boss")      # 45 cm: walkable onto


def finish(name, data_name, display_name, x1, half_y, levels, data_dir, room_center, room_extent, nav_center, nav_extent):
    exit_x = x1 + 400
    k.exit_gate(x1, half_opening=350, height=1350)
    k.exit_passage(x1, exit_x, height=H)
    return k.finish(name, data_name, display_name, unreal.FPSRLArenaType.OPEN, exit_x, room_center=room_center,
                    room_extent=room_extent, nav_center=nav_center, nav_extent=nav_extent, enemy_count=1, active_zones=1,
                    depths=(2,), room_type=unreal.RoomType.MINIBOSS, pool="miniboss_rooms", encounter=ENCOUNTER,
                    data_dir=data_dir)


def build_a():
    """A - Cross Court: 50 x 46 m, boss in the middle, platforms N / S / W / E, a full ring of open floor."""
    name, x0, x1, half_y, bx = "LV_Z1_Miniboss_01", 600.0, 5600.0, 2300.0, 3100.0
    k.begin(name, levels=ZONE1_LEVELS)
    # It was a small template room lit by fill_lights.py: its old fill lights and exposure volume go (the hall has its own).
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in sub.get_all_level_actors():
        if a.get_actor_label().startswith(("FillLight_", "RoomExposure")):
            sub.destroy_actor(a)
    k.vestibule(height=H)
    hall(x0, x1, half_y)
    boss(bx, 0)
    for tag, px, py in (("North", bx, 1900), ("South", bx, -1900), ("West", 1250, 0), ("East", 4950, 0)):
        k.shield_platform(tag, px, py, HALF, TOP, (bx, 0))
    # Four low blocks on the diagonals: something to move around (missiles are area attacks; these are landmarks).
    for sx in (1, -1):
        for sy in (1, -1):
            box("Block_%d_%d" % (sx, sy), bx + sx * 1100 - 150, bx + sx * 1100 + 150, sy * 1100 - 150, sy * 1100 + 150, 0, 120, M_FEATURE, "Cover")
    lights(x0, x1, half_y)
    return finish(name, "DA_Room_Z1_Miniboss_01", "Cross Court", x1, half_y, ZONE1_LEVELS, ZONE1_DATA,
                  (bx, 0, 450), (2700, 2500, 800), (bx, 0, 300), (2700, 2500, 600))


def build_b():
    """B - Diagonal Bastions: 48 x 48 m, platforms on the diagonals (NW / NE / SW / SE), the floor open between them."""
    name, x0, x1, half_y, bx = "LV_Miniboss_02_DiagonalBastions", 600.0, 5400.0, 2400.0, 3000.0
    k.begin(name)
    k.vestibule(height=H)
    hall(x0, x1, half_y)
    boss(bx, 0)
    for tag, sx, sy in (("NorthWest", -1, 1), ("NorthEast", 1, 1), ("SouthWest", -1, -1), ("SouthEast", 1, -1)):
        k.shield_platform(tag, bx + sx * 1400, sy * 1400, HALF, TOP, (bx, 0))
    # Low blocks on the axes, between the platforms.
    for tag, px, py in (("N", bx, 1250), ("S", bx, -1250), ("W", bx - 1250, 0), ("E", bx + 1250, 0)):
        box("Block_%s" % tag, px - 150, px + 150, py - 150, py + 150, 0, 120, M_FEATURE, "Cover")
    lights(x0, x1, half_y)
    return finish(name, "DA_Room_Miniboss_02_DiagonalBastions", "Diagonal Bastions", x1, half_y, None, None,
                  (bx, 0, 450), (2600, 2600, 800), (bx, 0, 300), (2600, 2600, 600))


def build_c():
    """C - Sunken Foundry: 50 x 50 m; the boss in a 16 x 16 m pit 1.5 m down (a ramp out of each side); the platforms
    are bastions against the outer walls at the compass points."""
    name, x0, x1, half_y, bx = "LV_Miniboss_03_SunkenFoundry", 600.0, 5600.0, 2500.0, 3100.0
    pit, depth = 800.0, -150.0
    k.begin(name)
    k.vestibule(height=H)
    hall(x0, x1, half_y, floor=False)
    # The ring floor around the pit, and the pit.
    box("Floor_W", x0, bx - pit, -half_y, half_y, depth - 40, 0, M_FLOOR, "Floor")
    box("Floor_E", bx + pit, x1, -half_y, half_y, depth - 40, 0, M_FLOOR, "Floor")
    box("Floor_N", bx - pit, bx + pit, pit, half_y, depth - 40, 0, M_FLOOR, "Floor")
    box("Floor_S", bx - pit, bx + pit, -half_y, -pit, depth - 40, 0, M_FLOOR, "Floor")
    box("Pit_Floor", bx - pit, bx + pit, -pit, pit, depth - 40, depth, M_FLOOR, "Pit")
    light("Pit_Light", bx, 0, 450, 12000.0, 1500.0)
    for tag, yaw, sx, sy in (("W", 180.0, -1, 0), ("E", 0.0, 1, 0), ("N", 90.0, 0, 1), ("S", -90.0, 0, -1)):
        # From the pit floor, 4.5 m in from its edge, up to the rim.
        k.ramp_dir("Pit_Ramp_%s" % tag, bx + sx * (pit - 450), sy * (pit - 450), yaw, 450, depth, 0, 400, M_FLOOR, "Pit")
    boss(bx, 0, floor_z=depth)
    for tag, px, py in (("North", bx, 2050), ("South", bx, -2050), ("West", 1150, 0), ("East", 5050, 0)):
        k.shield_platform(tag, px, py, HALF, TOP, (bx, 0))
    lights(x0, x1, half_y)
    return finish(name, "DA_Room_Miniboss_03_SunkenFoundry", "Sunken Foundry", x1, half_y, None, None,
                  (bx, 0, 450), (2700, 2700, 800), (bx, 0, 250), (2700, 2700, 650))


out = []
for build in (build_a, build_b, build_c):
    try:
        out += build()
    except Exception as ex:
        out.append("FAILED %s %s %s" % (build.__name__, ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
