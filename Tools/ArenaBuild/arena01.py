"""Arena 01 - Sunken Plaza (Open, 35 x 35 m). Run: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>."""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, spawn_point, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_01_SunkenPlaza"
EXIT_X = 4500.0
out = []
try:
    k.begin(NAME)
    k.vestibule()

    # Floors: a 35 x 35 m hall (X 600..4100), its middle a 19 m octagonal plaza sunk 1.2 m.
    box("Plaza_Floor", 1300, 3400, -1050, 1050, -160, -120, M_FLOOR, "Floor")
    box("Rim_West", 600, 1400, -1750, 1750, -160, 0, M_FLOOR, "Floor")
    box("Rim_East", 3300, 4100, -1750, 1750, -160, 0, M_FLOOR, "Floor")
    box("Rim_South", 1400, 3300, -1750, -950, -160, 0, M_FLOOR, "Floor")
    box("Rim_North", 1400, 3300, 950, 1750, -160, 0, M_FLOOR, "Floor")
    for i, (cx, cy) in enumerate(((1400, 950), (1400, -950), (3300, 950), (3300, -950))):
        boxc("Rim_Chamfer_%d" % i, cx, cy, -80, 566, 566, 160, M_FLOOR, "Floor", yaw=45)
    for tag, x0, x1 in (("W", 1300, 1700), ("E", 3000, 3400)):
        box("Alcove_N%s_Floor" % tag, x0, x1, 1750, 2450, -40, 0, M_FLOOR, "Floor")
        box("Alcove_S%s_Floor" % tag, x0, x1, -2450, -1750, -40, 0, M_FLOOR, "Floor")

    # Stairs into the plaza (rim 0 -> plaza -120).
    for s in range(1, 4):
        top = -30.0 * s
        box("Stairs_W_%d" % s, 1400 + 50 * (s - 1), 1400 + 50 * s, -250, 250, -160, top, M_FLOOR, "Stairs")
        box("Stairs_E_%d" % s, 3300 - 50 * s, 3300 - 50 * (s - 1), -250, 250, -160, top, M_FLOOR, "Stairs")
        box("Stairs_N_%d" % s, 2100, 2600, 950 - 50 * s, 950 - 50 * (s - 1), -160, top, M_FLOOR, "Stairs")
        box("Stairs_S_%d" % s, 2100, 2600, -950 + 50 * (s - 1), -950 + 50 * s, -160, top, M_FLOOR, "Stairs")

    # Landmark: broken monolith on a dais.
    cyl("Monolith_Dais", 2350, 0, -120, 640, 40, M_FEATURE, "Landmark")
    boxc("Monolith_Shaft", 2350, 0, 330, 240, 240, 820, M_FEATURE, "Landmark", yaw=12)
    boxc("Monolith_Crown", 2385, 25, 840, 290, 290, 220, M_FEATURE, "Landmark", yaw=33, pitch=9, roll=6)
    boxc("Monolith_Fragment", 2860, 40, -35, 430, 210, 170, M_FEATURE, "Landmark", yaw=68)
    light("Monolith_Light", 2350, 0, 1000, 25000.0, 3200.0)

    # Plaza columns and rim walls (cover).
    for i, (x, y, h) in enumerate(((2810, 460, 420), (1890, 460, 260), (1890, -460, 160), (2810, -460, 340))):
        cyl("Plaza_Column_%d" % i, x, y, -120, 180, h, M_FEATURE, "Cover")
    for i, (cx, cy, yaw) in enumerate(((1579, 771, 45), (3121, 771, -45), (1579, -771, -45), (3121, -771, 45))):
        boxc("Rim_Wall_Diag_%d" % i, cx, cy, 55, 560, 40, 110, M_WALL, "Cover", yaw=yaw)
    for i, (x0, x1) in enumerate(((1850, 2050), (2650, 2850))):
        box("Rim_Wall_N_%d" % i, x0, x1, 965, 1005, 0, 110, M_WALL, "Cover")
        box("Rim_Wall_S_%d" % i, x0, x1, -1005, -965, 0, 110, M_WALL, "Cover")
    for i, (y0, y1) in enumerate(((300, 550), (-550, -300))):
        box("Rim_Wall_W_%d" % i, 1345, 1385, y0, y1, 0, 110, M_WALL, "Cover")
        box("Rim_Wall_E_%d" % i, 3315, 3355, y0, y1, 0, 110, M_WALL, "Cover")

    # Open elevation (user rule, 2026-10-01): a terrace 2.5 m up on the north and south rims (between the arches),
    # overlooking the plaza, stairs at both ends, open edges (dropping off is just a drop), pillars on top to move around.
    T_Z = 250.0
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        y0, y1 = sorted((1250 * sign, 1750 * sign))
        box("Terrace_%s_Deck" % tag, 2050, 2650, y0, y1, -160, T_Z, M_FLOOR, "Terraces")
        for s in range(1, 11):   # 10 steps of 25 cm each side, along X
            box("Terrace_%s_StairW_%d" % (tag, s), 1700 + 35 * (s - 1), 1700 + 35 * s, y0, y1, -160, 25.0 * s, M_FLOOR, "Terraces")
            box("Terrace_%s_StairE_%d" % (tag, s), 3000 - 35 * s, 3000 - 35 * (s - 1), y0, y1, -160, 25.0 * s, M_FLOOR, "Terraces")
        for x in (2200, 2500):
            cyl("Terrace_%s_Pillar_%d" % (tag, x), x, 1500 * sign, T_Z, 140, 300, M_FEATURE, "Terraces")

    # Outer walls: entrance (west), exit (east), four arches (north / south).
    H = 700
    box("Wall_W_N", 500, 600, 400, 1800, 0, H, M_WALL, "Walls")
    box("Wall_W_S", 500, 600, -1800, -400, 0, H, M_WALL, "Walls")
    box("Wall_W_Lintel", 500, 600, -400, 400, 500, H, M_WALL, "Walls")
    box("Wall_E_N", 4100, 4200, 350, 1800, 0, H, M_WALL, "Walls")
    box("Wall_E_S", 4100, 4200, -1800, -350, 0, H, M_WALL, "Walls")
    for side, y0, y1 in (("N", 1750, 1850), ("S", -1850, -1750)):
        box("Wall_%s_1" % side, 500, 1300, y0, y1, 0, H, M_WALL, "Walls")
        box("Wall_%s_2" % side, 1700, 3000, y0, y1, 0, H, M_WALL, "Walls")
        box("Wall_%s_3" % side, 3400, 4200, y0, y1, 0, H, M_WALL, "Walls")
        box("Wall_%s_ArchW_Lintel" % side, 1300, 1700, y0, y1, 450, H, M_WALL, "Walls")
        box("Wall_%s_ArchE_Lintel" % side, 3000, 3400, y0, y1, 450, H, M_WALL, "Walls")
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
            cx = (x0 + x1) / 2.0
            yaw = -90.0 if sign > 0 else 90.0
            spawn_point(zone, cx - 90, 2050 * sign, yaw)
            spawn_point(zone, cx + 90, 2050 * sign, yaw)
            spawn_point(zone, cx, 2250 * sign, yaw)
            light("Alcove_%s%s_Light" % (side, tag), cx, 2150 * sign, 520, 6000.0, 1100.0)
    for i, (x, y) in enumerate(((575, 1775), (575, -1775), (4125, 1775), (4125, -1775))):
        boxc("Buttress_%d" % i, x, y, 450, 300, 300, 900, M_WALL, "Silhouette")

    # Flow: the exit gate towers over the monolith; runners lead from the entrance to the plaza and on to the exit.
    k.exit_gate(4100, half_opening=350, height=1300)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_West", 600, 1400)
    k.runner("Runner_East", 3300, 4100)
    k.exit_passage(4100, EXIT_X)

    out += k.finish(NAME, "DA_Room_Arena01_SunkenPlaza", "Sunken Plaza", unreal.FPSRLArenaType.OPEN, EXIT_X,
                    room_center=(2350, 0, 350), room_extent=(2400, 2600, 700), nav_center=(2300, 0, 300), nav_extent=(2450, 2600, 700),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
