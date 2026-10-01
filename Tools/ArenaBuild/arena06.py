"""Arena 06 - Sundered Halls (Multi-space, 50 x 40 m).

Three spaces in a row, joined by several openings, so a fight spills from one into the next:
- the arrival court (west): open, with low walls near the entrance and two large blocks;
- the halls (middle), three parallel spaces divided by lines of massive pillars (reworked 2026-10-01 from 9 m walls:
  open between spaces, user rule):
    the nave on the axis (a toppled statue off the axis is the landmark),
    the north wing with a raised gallery (2 m, stairs at both ends) overlooking its floor,
    the south wing, a hall of pillars;
- the far court (east): the exit gate, gates in its side walls and doors beside the exit.
Enemies arrive at the gallery door, the pillar hall, the far court's side gates and the doors beside the exit, never in
the arrival court. Flow: the nave openings are full-height slots on the axis, so the exit gate shows through them from the entrance; a
runner leads straight through all three spaces.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, spawn_point, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_06_SunderedHalls"
HALL_END = 5600.0
EXIT_X = 6000.0
H = 900
GAL_Z = 200.0
out = []


def wall_x(label, x0, x1, segments):
    """A wall across the hall (Y runs), segments = (y0, y1, z0) with z0 > 0 for an opening's lintel."""
    for i, (y0, y1, z0) in enumerate(segments):
        box("%s_%d" % (label, i), x0, x1, y0, y1, z0, H, M_WALL, "Walls")


def wall_y(label, y0, y1, segments, top=H):
    """A wall along the hall (X runs), segments = (x0, x1, z0, z1)."""
    for i, (x0, x1, z0, z1) in enumerate(segments):
        box("%s_%d" % (label, i), x0, x1, y0, y1, z0, z1, M_WALL, "Walls")


def alcove(zone, x0, x1, sign, floor_z=0.0, depth=700.0, points=((0.3, 0.45), (0.7, 0.45), (0.5, 0.75))):
    """A spawn room behind a side wall (sign +1 north, -1 south), open toward the hall across X x0..x1."""
    tag = "%s_%s" % (zone, "N" if sign > 0 else "S")
    a, b = sorted((2000 * sign, (2100 + depth) * sign))
    box("%s_Floor" % tag, x0, x1, a, b, floor_z - 40, floor_z, M_FLOOR, "Alcoves")
    c, d = sorted((2100 * sign, (2100 + depth) * sign))
    box("%s_SideA" % tag, x0 - 100, x0, c, d, 0, H, M_WALL, "Alcoves")
    box("%s_SideB" % tag, x1, x1 + 100, c, d, 0, H, M_WALL, "Alcoves")
    e, f = sorted(((2100 + depth) * sign, (2200 + depth) * sign))
    box("%s_Back" % tag, x0 - 100, x1 + 100, e, f, 0, H, M_WALL, "Alcoves")
    g, h = sorted((2050 * sign, (2200 + depth) * sign))
    box("%s_Roof" % tag, x0 - 100, x1 + 100, g, h, floor_z + 450, floor_z + 510, M_WALL, "Alcoves")
    for fx, fy in points:
        spawn_point(zone, x0 + (x1 - x0) * fx, (2100 + depth * fy) * sign, -90.0 * sign, floor_z=floor_z)
    light("%s_Light" % tag, (x0 + x1) / 2.0, (2100 + depth * 0.6) * sign, floor_z + 380, 6000.0, 1000.0)


try:
    k.begin(NAME)
    k.vestibule(height=700)
    box("Floor", 600, HALL_END, -2000, 2000, -40, 0, M_FLOOR, "Floor")

    # --------------------------------------------------------------- outer walls (with the spawn openings)
    wall_x("Wall_W", 500, 600, ((400, 2100, 0), (-2100, -400, 0), (-400, 400, 500)))
    wall_y("Wall_N", 2000, 2100, ((500, 3100, 0, H), (3100, 3400, 0, GAL_Z), (3100, 3400, GAL_Z + 400, H),
                                  (3400, 4600, 0, H), (4600, 5000, 450, H), (5000, 5700, 0, H)))
    wall_y("Wall_S", -2100, -2000, ((500, 3300, 0, H), (3300, 3700, 450, H), (3700, 4600, 0, H), (4600, 5000, 450, H),
                                    (5000, 5700, 0, H)))
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        for y0, y1, z0 in ((350, 900, 0), (900, 1300, 450), (1300, 2000, 0)):
            a, b = sorted((y0 * sign, y1 * sign))
            box("Wall_E_%s_%d" % (tag, y0), HALL_END, HALL_END + 100, a, b, z0, H, M_WALL, "Walls")

    # --------------------------------------------------------------- the three spaces, divided by pillar lines
    # Rework (user rule, 2026-10-01: open, not walled-off): the 9 m walls between the arrival court, the halls and the
    # far court, and between the nave and the wings, became lines of massive pillars. The spaces still read as separate,
    # but you see, shoot and move between them anywhere. The axis stays clear (the exit gate in view from the entrance).
    for line, x in (("W", 2050), ("E", 4050)):
        for y in (-1800, -1350, -900, -500, 500, 900, 1350, 1800):
            if line == "E" and 1250 <= y <= 1750:
                continue   # the gallery's east stairs come down here
            cyl("Cross_%s_Pillar_%d" % (line, y), x, y, 0, 220, H, M_WALL, "Pillars")
        for y0, y1 in ((-1350, -900), (900, 1350)):   # low ruined wall between two of the pillars: cover
            box("Cross_%s_Ruin_%d" % (line, y0), x - 50, x + 50, y0 + 110, y1 - 110, 0, 120, M_WALL, "Pillars")
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        for x in range(2250, 4000, 380):
            cyl("Wing_%s_Pillar_%d" % (tag, x), x, 700 * sign, 0, 160, H, M_WALL, "Pillars")

    # --------------------------------------------------------------- arrival court (no spawns here)
    box("Entry_Wall_N", 1150, 1200, 250, 650, 0, 110, M_WALL, "Cover")
    box("Entry_Wall_S", 1150, 1200, -650, -250, 0, 110, M_WALL, "Cover")
    box("Court_Block_N", 1450, 1850, 1150, 1450, 0, 220, M_FEATURE, "Cover")
    box("Court_Block_S", 1450, 1850, -1450, -1150, 0, 220, M_FEATURE, "Cover")
    cyl("Court_Well", 1500, -650, 0, 260, 90, M_FEATURE, "Cover")

    # --------------------------------------------------------------- the nave: landmark (a toppled statue) off the axis
    box("Statue_Base", 2850, 3250, -600, -260, 0, 160, M_FEATURE, "Landmark")
    boxc("Statue_Body", 3050, -430, 160 + 560, 200, 200, 1120, M_FEATURE, "Landmark", roll=-7.0)
    boxc("Statue_Head", 3600, -480, 90, 220, 200, 180, M_FEATURE, "Landmark", yaw=20.0)
    light("Statue_Light", 3050, -300, 1250, 14000.0, 2000.0)
    box("Nave_Block_NW", 2350, 2550, 300, 450, 0, 110, M_FEATURE, "Cover")
    box("Nave_Block_NE", 3550, 3750, 300, 450, 0, 110, M_FEATURE, "Cover")

    # --------------------------------------------------------------- north wing: the gallery (stairs at both ends)
    for s in range(1, 9):
        box("Gallery_StairsW_%d" % s, 2300 + 60 * (s - 1), 2300 + 60 * s, 1300, 1700, 0, 25.0 * s, M_FLOOR, "Gallery")
    box("Gallery_Deck", 2780, 3520, 1300, 2000, -40, GAL_Z, M_FLOOR, "Gallery")
    for s in range(1, 8):
        box("Gallery_StairsE_%d" % s, 3520 + 60 * (s - 1), 3520 + 60 * s, 1300, 1700, 0, GAL_Z - 25.0 * s, M_FLOOR, "Gallery")
    box("Gallery_Parapet", 2780, 3520, 1280, 1300, GAL_Z, GAL_Z + 100, M_WALL, "Gallery")
    box("NorthWing_Crate_1", 2950, 3150, 900, 1100, 0, 110, M_FEATURE, "Cover")
    box("NorthWing_Crate_2", 3300, 3450, 850, 1000, 0, 110, M_FEATURE, "Cover")
    light("NorthWing_Light", 3150, 1050, 650, 5000.0, 1200.0)
    alcove("Gallery", 3100, 3400, 1, floor_z=GAL_Z, depth=600)

    # --------------------------------------------------------------- south wing: the pillar hall
    for x in (2400, 2900, 3400, 3800):
        for y in (-1050, -1650):
            cyl("Pillar_%d_%d" % (x, -y), x, y, 0, 160, H, M_WALL, "PillarHall")
    box("PillarHall_Block_1", 2550, 2800, -1420, -1280, 0, 110, M_FEATURE, "Cover")
    box("PillarHall_Block_2", 3500, 3700, -1420, -1280, 0, 110, M_FEATURE, "Cover")
    light("PillarHall_Light", 3100, -1350, 650, 5000.0, 1200.0)
    alcove("PillarHall", 3300, 3700, -1)

    # --------------------------------------------------------------- far court: side gates, doors beside the exit
    box("FarCourt_Block_N", 4500, 4800, 650, 950, 0, 120, M_FEATURE, "Cover")
    box("FarCourt_Block_S", 4500, 4800, -950, -650, 0, 120, M_FEATURE, "Cover")
    cyl("FarCourt_Column_N", 5100, 1350, 0, 200, 600, M_FEATURE, "Cover")
    cyl("FarCourt_Column_S", 5100, -1350, 0, 200, 600, M_FEATURE, "Cover")
    for sign in (1, -1):
        alcove("CourtGates", 4600, 5000, sign, points=((0.3, 0.5), (0.7, 0.5)))
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        a3, b3 = sorted((900 * sign, 1300 * sign))
        box("FarDoor_%s_Floor" % tag, HALL_END, HALL_END + 700, a3, b3, -40, 0, M_FLOOR, "Alcoves")
        a4, b4 = sorted((800 * sign, 900 * sign))
        box("FarDoor_%s_SideA" % tag, HALL_END + 100, HALL_END + 700, a4, b4, 0, H, M_WALL, "Alcoves")
        a5, b5 = sorted((1300 * sign, 1400 * sign))
        box("FarDoor_%s_SideB" % tag, HALL_END + 100, HALL_END + 700, a5, b5, 0, H, M_WALL, "Alcoves")
        a6, b6 = sorted((800 * sign, 1400 * sign))
        box("FarDoor_%s_Back" % tag, HALL_END + 700, HALL_END + 800, a6, b6, 0, H, M_WALL, "Alcoves")
        box("FarDoor_%s_Roof" % tag, HALL_END + 50, HALL_END + 800, a6, b6, 450, 510, M_WALL, "Alcoves")
        spawn_point("FarDoors", HALL_END + 350, 1020 * sign, 180.0)
        spawn_point("FarDoors", HALL_END + 350, 1180 * sign, 180.0)
        light("FarDoor_%s_Light" % tag, HALL_END + 400, 1100 * sign, 380, 6000.0, 1000.0)

    # --------------------------------------------------------------- flow: exit gate + runner through all three spaces
    k.exit_gate(HALL_END, half_opening=350, height=1500)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_Axis", 600, HALL_END)
    k.exit_passage(HALL_END, EXIT_X, height=700)

    out += k.finish(NAME, "DA_Room_Arena06_SunderedHalls", "Sundered Halls", unreal.FPSRLArenaType.MULTI_SPACE, EXIT_X,
                    room_center=(3150, 0, 450), room_extent=(2700, 2200, 900), nav_center=(3150, 0, 300), nav_extent=(3350, 3000, 700),
                    enemy_count=8, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
