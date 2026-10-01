"""Arena 03 - Gatehouse (Chokepoint, 40 x 25 m).

West chamber (arrival) -> a 4 m thick gatehouse wall -> east chamber (enemies, exit). Three ways through the
gatehouse: the main gate tunnel (6 m wide, on the axis), a narrow south breach (2 m), and a raised north gallery (3 m up)
that crosses into the east chamber and overlooks it. Hold the gate, push it, slip the breach or flank from above.
Enemies come from an upper door onto the gallery (they can flank too), a south hall and two tunnels beside the exit.
Flow: the runner runs through the gate tunnel and under a ruined arch to the exit gate, which towers over the gatehouse.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, spawn_point, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_03_Gatehouse"
EXIT_X = 5000.0
H = 800
GH0, GH1, GH_TOP = 1900, 2300, 1000     # gatehouse wall X range and height
GAL_Z = 300.0                           # gallery walking height
out = []
try:
    k.begin(NAME)
    k.vestibule(height=H)

    # Floors.
    box("Floor_West", 600, GH0, -1250, 1250, -40, 0, M_FLOOR, "Floor")
    box("Floor_Gate", GH0, GH1, -1250, 1250, -40, 0, M_FLOOR, "Floor")
    box("Floor_East", GH1, 4600, -1250, 1250, -40, 0, M_FLOOR, "Floor")

    # --------------------------------------------------------------- the gatehouse wall and its three crossings
    box("Gatehouse_S_Outer", GH0, GH1, -1250, -1050, 0, GH_TOP, M_WALL, "Gatehouse")
    box("Gatehouse_Breach_Lintel", GH0, GH1, -1050, -850, 280, GH_TOP, M_WALL, "Gatehouse")
    box("Gatehouse_S_Inner", GH0, GH1, -850, -300, 0, GH_TOP, M_WALL, "Gatehouse")
    box("Gatehouse_Gate_Lintel", GH0, GH1, -300, 300, 450, GH_TOP, M_WALL, "Gatehouse")
    box("Gatehouse_N_Inner", GH0, GH1, 300, 950, 0, GH_TOP, M_WALL, "Gatehouse")
    box("Gatehouse_Gallery_Lintel", GH0, GH1, 950, 1250, GAL_Z + 250, GH_TOP, M_WALL, "Gatehouse")
    box("Breach_Rubble", GH0 + 60, GH1 - 120, -1040, -900, 0, 45, M_FEATURE, "Gatehouse")
    for sign in (1, -1):   # towers framing the gate on the west face (still lower than the exit gate)
        y0, y1 = sorted((300 * sign, 620 * sign))
        box("GateTower_%s" % ("N" if sign > 0 else "S"), GH0 - 120, GH0, y0, y1, 0, 1200, M_FEATURE, "Gatehouse")
    for i, y in enumerate(range(-1150, 1251, 340)):   # crenellations along the top
        box("Crenel_%d" % i, GH0 + 40, GH1 - 40, y - 80, y + 80, GH_TOP, GH_TOP + 160, M_WALL, "Gatehouse")
    light("GateTunnel_Light", 2100, 0, 380, 9000.0, 900.0)

    # --------------------------------------------------------------- north gallery (raised flank route)
    for s in range(1, 13):   # west stairs up (X 900..1300)
        box("Gallery_StairW_%d" % s, 900 + 33.33 * (s - 1), 900 + 33.33 * s, 950, 1250, 0, 25.0 * s, M_FLOOR, "Gallery")
    box("Gallery_Deck", 1300, 3500, 950, 1250, 0, GAL_Z, M_FLOOR, "Gallery")
    for s in range(1, 12):   # east stairs down (X 3500..3900)
        box("Gallery_StairE_%d" % s, 3500 + 33.33 * (s - 1), 3500 + 33.33 * s, 950, 1250, 0, GAL_Z - 25.0 * s, M_FLOOR, "Gallery")
    for i, (x0, x1) in enumerate(((1300, 1800), (2400, 2800), (3000, 3400))):
        box("Gallery_Parapet_%d" % i, x0, x1, 930, 950, GAL_Z, GAL_Z + 100, M_WALL, "Gallery")

    # --------------------------------------------------------------- west chamber (arrival): hold the gate
    box("Barricade_N", 1500, 1560, 350, 800, 0, 120, M_FEATURE, "Cover")
    box("Barricade_S", 1500, 1560, -800, -350, 0, 120, M_FEATURE, "Cover")
    cyl("West_BrokenPillar", 1100, -700, 0, 200, 380, M_FEATURE, "Cover")

    # --------------------------------------------------------------- east chamber: ruined arch on the axis, heavy blocks
    for sign in (1, -1):
        y0, y1 = sorted((350 * sign, 650 * sign))
        box("Arch_Pier_%s" % ("N" if sign > 0 else "S"), 3200, 3500, y0, y1, 0, 700, M_WALL, "Landmark")
    box("Arch_Lintel_Broken", 3200, 3500, -650, 120, 600, 760, M_WALL, "Landmark")
    boxc("Arch_Fallen_Piece", 3650, 420, 75, 420, 160, 150, M_FEATURE, "Landmark", yaw=-20)
    box("Block_SW", 2700, 3100, -1000, -750, 0, 110, M_FEATURE, "Cover")
    box("Block_NE", 3800, 4100, 450, 750, 0, 130, M_FEATURE, "Cover")
    boxc("Block_SE_Fallen", 4000, -500, 75, 500, 200, 150, M_FEATURE, "Cover", yaw=25)

    # --------------------------------------------------------------- outer walls
    box("Wall_W_N", 500, 600, 400, 1250, 0, H, M_WALL, "Walls")
    box("Wall_W_S", 500, 600, -1250, -400, 0, H, M_WALL, "Walls")
    box("Wall_W_Lintel", 500, 600, -400, 400, 500, H, M_WALL, "Walls")
    # North wall: solid, except an upper door onto the gallery (X 2900..3300, from the gallery up).
    box("Wall_N_1", 500, 2900, 1250, 1350, 0, H, M_WALL, "Walls")
    box("Wall_N_2", 3300, 4700, 1250, 1350, 0, H, M_WALL, "Walls")
    box("Wall_N_UpperDoor_Below", 2900, 3300, 1250, 1350, 0, GAL_Z, M_WALL, "Walls")
    box("Wall_N_UpperDoor_Lintel", 2900, 3300, 1250, 1350, GAL_Z + 300, H, M_WALL, "Walls")
    # South wall with the south hall door (X 3300..3700).
    box("Wall_S_1", 500, 3300, -1350, -1250, 0, H, M_WALL, "Walls")
    box("Wall_S_2", 3700, 4700, -1350, -1250, 0, H, M_WALL, "Walls")
    box("Wall_S_Door_Lintel", 3300, 3700, -1350, -1250, 450, H, M_WALL, "Walls")
    # East wall: exit opening (±350) and the two tunnels (Y 700..1100).
    for sign in (1, -1):
        for y0, y1 in ((1100, 1250), (350, 700)):
            a, b = sorted((y0 * sign, y1 * sign))
            box("Wall_E_%d_%d" % (sign, y0), 4600, 4700, a, b, 0, H, M_WALL, "Walls")
        a, b = sorted((700 * sign, 1100 * sign))
        box("Wall_E_Tunnel_Lintel_%d" % sign, 4600, 4700, a, b, 400, H, M_WALL, "Walls")

    # --------------------------------------------------------------- spawn zones
    # Gallery: an upper room behind the north wall, at gallery height.
    box("UpperRoom_Floor", 2900, 3300, 1350, 1850, GAL_Z - 40, GAL_Z, M_FLOOR, "Alcoves")
    box("UpperRoom_SideA", 2800, 2900, 1350, 1850, 0, H, M_WALL, "Alcoves")
    box("UpperRoom_SideB", 3300, 3400, 1350, 1850, 0, H, M_WALL, "Alcoves")
    box("UpperRoom_Back", 2800, 3400, 1850, 1950, 0, H, M_WALL, "Alcoves")
    box("UpperRoom_Roof", 2800, 3400, 1300, 1950, H, H + 60, M_WALL, "Alcoves")
    for x, y in ((3010, 1550), (3190, 1550), (3100, 1720)):
        spawn_point("Gallery", x, y, -90.0, floor_z=GAL_Z)
    light("UpperRoom_Light", 3100, 1650, GAL_Z + 260, 6000.0, 900.0)
    # South hall.
    box("SouthHall_Floor", 3300, 3700, -1950, -1250, -40, 0, M_FLOOR, "Alcoves")
    box("SouthHall_SideA", 3200, 3300, -1950, -1350, 0, H, M_WALL, "Alcoves")
    box("SouthHall_SideB", 3700, 3800, -1950, -1350, 0, H, M_WALL, "Alcoves")
    box("SouthHall_Back", 3200, 3800, -2050, -1950, 0, H, M_WALL, "Alcoves")
    box("SouthHall_Roof", 3200, 3800, -2050, -1300, H, H + 60, M_WALL, "Alcoves")
    for x, y in ((3410, -1600), (3590, -1600), (3500, -1800)):
        spawn_point("SouthHall", x, y, 90.0)
    light("SouthHall_Light", 3500, -1700, 560, 6000.0, 1000.0)
    # Tunnels beside the exit.
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        a, b = sorted((700 * sign, 1100 * sign))
        box("Tunnel_%s_Floor" % tag, 4600, 5300, a, b, -40, 0, M_FLOOR, "Alcoves")
        a2, b2 = sorted((600 * sign, 700 * sign))
        box("Tunnel_%s_SideA" % tag, 4700, 5300, a2, b2, 0, H, M_WALL, "Alcoves")
        a3, b3 = sorted((1100 * sign, 1200 * sign))
        box("Tunnel_%s_SideB" % tag, 4700, 5300, a3, b3, 0, H, M_WALL, "Alcoves")
        a4, b4 = sorted((600 * sign, 1200 * sign))
        box("Tunnel_%s_Back" % tag, 5300, 5400, a4, b4, 0, H, M_WALL, "Alcoves")
        box("Tunnel_%s_Roof" % tag, 4650, 5400, a4, b4, 400, 460, M_WALL, "Alcoves")
        spawn_point("EastTunnels", 4950, 820 * sign, 180.0)
        spawn_point("EastTunnels", 4950, 980 * sign, 180.0)
        light("Tunnel_%s_Light" % tag, 5000, 900 * sign, 330, 5000.0, 900.0)

    # --------------------------------------------------------------- flow: exit gate + runner through the gate tunnel
    k.exit_gate(4600, half_opening=350, height=1400)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_Axis", 600, 4600)
    k.exit_passage(4600, EXIT_X, height=H)

    out += k.finish(NAME, "DA_Room_Arena03_Gatehouse", "Gatehouse", unreal.FPSRLArenaType.CHOKEPOINT, EXIT_X,
                    room_center=(2700, 0, 400), room_extent=(2800, 2100, 800), nav_center=(2700, 0, 300), nav_extent=(2900, 2200, 700),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
