"""Arena 04 - Cistern Tiers (Vertical, 30 x 30 m).

Three levels: a 14 x 14 m pit (3 m down, two stairways), the main floor ring around it (covered walkways under the
balconies), and balconies 4 m up on the north and south sides (long stairs), joined by a bridge over the pit. A cistern
column rises from the pit floor as the landmark. Enemies come from tunnels at the bottom of the pit, from upper doors onto
the balconies, and from two gates beside the exit. High ground sees everything and is exposed; the pit is protected and
surrounded. Flow: runners on the main floor along the axis, under the bridge, to the exit gate.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, spawn_point, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_04_CisternTiers"
EXIT_X = 4000.0
H = 1000
PIT_Z = -300.0
BAL_Z = 400.0
out = []
try:
    k.begin(NAME)
    k.vestibule(height=700)

    # --------------------------------------------------------------- main floor ring (thin slab) and the pit
    box("Ring_West", 600, 1400, -1500, 1500, -40, 0, M_FLOOR, "Floor")
    box("Ring_East", 2800, 3600, -1500, 1500, -40, 0, M_FLOOR, "Floor")
    box("Ring_North", 1400, 2800, 700, 1500, -40, 0, M_FLOOR, "Floor")
    box("Ring_South", 1400, 2800, -1500, -700, -40, 0, M_FLOOR, "Floor")
    box("Pit_Floor", 1400, 2800, -700, 700, PIT_Z - 40, PIT_Z, M_FLOOR, "Pit")
    box("Pit_Wall_W", 1360, 1400, -700, 700, PIT_Z, -40, M_WALL, "Pit")
    box("Pit_Wall_E", 2800, 2840, -700, 700, PIT_Z, -40, M_WALL, "Pit")
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        y0, y1 = sorted((700 * sign, 740 * sign))
        box("Pit_Wall_%s_1" % tag, 1400, 1900, y0, y1, PIT_Z, -40, M_WALL, "Pit")
        box("Pit_Wall_%s_2" % tag, 2300, 2800, y0, y1, PIT_Z, -40, M_WALL, "Pit")
        # Tunnel under the ring into the pit (enemies come up from below).
        a, b = sorted((700 * sign, 1250 * sign))
        box("PitTunnel_%s_Floor" % tag, 1900, 2300, a, b, PIT_Z - 40, PIT_Z, M_FLOOR, "Pit")
        box("PitTunnel_%s_SideA" % tag, 1850, 1900, a, b, PIT_Z, -40, M_WALL, "Pit")
        box("PitTunnel_%s_SideB" % tag, 2300, 2350, a, b, PIT_Z, -40, M_WALL, "Pit")
        c, d = sorted((1250 * sign, 1300 * sign))
        box("PitTunnel_%s_Back" % tag, 1850, 2350, c, d, PIT_Z, -40, M_WALL, "Pit")
        for x, y in ((2000, 1000), (2200, 1000), (2100, 1150)):
            spawn_point("PitTunnels", x, y * sign, -90.0 * sign, floor_z=PIT_Z)
        light("PitTunnel_%s_Light" % tag, 2100, 1050 * sign, PIT_Z + 200, 7000.0, 800.0)
    # Stairways down into the pit (12 steps of 25 cm), along the west and east pit walls.
    for s in range(1, 13):
        box("PitStairs_W_%d" % s, 1400, 1700, 700 - 60 * s, 700 - 60 * (s - 1), PIT_Z - 40, -25.0 * s, M_FLOOR, "Stairs")
        box("PitStairs_E_%d" % s, 2500, 2800, -700 + 60 * (s - 1), -700 + 60 * s, PIT_Z - 40, -25.0 * s, M_FLOOR, "Stairs")
    # Landmark: the cistern column rising from the pit floor, with low cover around its base.
    cyl("Cistern_Column", 1950, -330, PIT_Z, 360, 1200, M_FEATURE, "Landmark")   # off the axis: the exit gate stays in view
    cyl("Cistern_Capital", 1950, -330, PIT_Z + 1200, 520, 70, M_FEATURE, "Landmark")
    light("Cistern_Light", 1950, -330, PIT_Z + 1400, 20000.0, 2600.0)
    box("Pit_Block_1", 2200, 2420, -620, -420, PIT_Z, PIT_Z + 110, M_FEATURE, "Cover")
    box("Pit_Block_2", 2250, 2450, 280, 520, PIT_Z, PIT_Z + 110, M_FEATURE, "Cover")

    # --------------------------------------------------------------- balconies, their stairs, and the bridge
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        a, b = sorted((1100 * sign, 1500 * sign))
        for s in range(1, 17):   # stairs up along the outer wall (X 600..1560)
            box("BalconyStairs_%s_%d" % (tag, s), 600 + 60 * (s - 1), 600 + 60 * s, a, b, 0, 25.0 * s, M_FLOOR, "Balconies")
        box("Balcony_%s_Deck" % tag, 1560, 3400, a, b, BAL_Z - 40, BAL_Z, M_FLOOR, "Balconies")
        for x in (1650, 2500, 3330):
            cyl("Balcony_%s_Support_%d" % (tag, x), x, 1300 * sign, 0, 120, BAL_Z - 40, M_WALL, "Balconies")
        e, f = sorted((1080 * sign, 1100 * sign))
        for i, (x0, x1) in enumerate(((1560, 2300), (2850, 3400))):   # open where the bridge joins
            box("Balcony_%s_Parapet_%d" % (tag, i), x0, x1, e, f, BAL_Z, BAL_Z + 100, M_WALL, "Balconies")
    box("Bridge_Deck", 2400, 2750, -1100, 1100, BAL_Z - 40, BAL_Z, M_FLOOR, "Bridge")
    box("Bridge_Parapet_W", 2380, 2400, -1100, 1100, BAL_Z, BAL_Z + 100, M_WALL, "Bridge")
    box("Bridge_Parapet_E", 2750, 2770, -1100, 1100, BAL_Z, BAL_Z + 100, M_WALL, "Bridge")

    # --------------------------------------------------------------- outer walls with the balcony doors and east gates
    box("Wall_W_N", 500, 600, 400, 1500, 0, H, M_WALL, "Walls")
    box("Wall_W_S", 500, 600, -1500, -400, 0, H, M_WALL, "Walls")
    box("Wall_W_Lintel", 500, 600, -400, 400, 500, H, M_WALL, "Walls")
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        a, b = sorted((1500 * sign, 1600 * sign))
        box("Wall_%s_1" % tag, 500, 2700, a, b, 0, H, M_WALL, "Walls")
        box("Wall_%s_2" % tag, 3100, 3700, a, b, 0, H, M_WALL, "Walls")
        box("Wall_%s_DoorBelow" % tag, 2700, 3100, a, b, 0, BAL_Z, M_WALL, "Walls")
        box("Wall_%s_DoorLintel" % tag, 2700, 3100, a, b, BAL_Z + 300, H, M_WALL, "Walls")
        # Upper room behind the balcony door.
        c, d = sorted((1600 * sign, 2100 * sign))
        box("UpperRoom_%s_Floor" % tag, 2700, 3100, c, d, BAL_Z - 40, BAL_Z, M_FLOOR, "Alcoves")
        box("UpperRoom_%s_SideA" % tag, 2600, 2700, c, d, 0, H, M_WALL, "Alcoves")
        box("UpperRoom_%s_SideB" % tag, 3100, 3200, c, d, 0, H, M_WALL, "Alcoves")
        e, f = sorted((2100 * sign, 2200 * sign))
        box("UpperRoom_%s_Back" % tag, 2600, 3200, e, f, 0, H, M_WALL, "Alcoves")
        g, h = sorted((1550 * sign, 2200 * sign))
        box("UpperRoom_%s_Roof" % tag, 2600, 3200, g, h, BAL_Z + 360, BAL_Z + 420, M_WALL, "Alcoves")
        spawn_point("Balconies", 2810, 1800 * sign, -90.0 * sign, floor_z=BAL_Z)
        spawn_point("Balconies", 2990, 1800 * sign, -90.0 * sign, floor_z=BAL_Z)
        light("UpperRoom_%s_Light" % tag, 2900, 1900 * sign, BAL_Z + 250, 6000.0, 900.0)
        # East gates beside the exit.
        for y0, y1 in ((1100, 1500), (350, 700)):
            a2, b2 = sorted((y0 * sign, y1 * sign))
            box("Wall_E_%s_%d" % (tag, y0), 3600, 3700, a2, b2, 0, H, M_WALL, "Walls")
        a3, b3 = sorted((700 * sign, 1100 * sign))
        box("Wall_E_Gate_%s_Lintel" % tag, 3600, 3700, a3, b3, 400, H, M_WALL, "Walls")
        box("EastGate_%s_Floor" % tag, 3600, 4300, a3, b3, -40, 0, M_FLOOR, "Alcoves")
        a4, b4 = sorted((600 * sign, 700 * sign))
        box("EastGate_%s_SideA" % tag, 3700, 4300, a4, b4, 0, H, M_WALL, "Alcoves")
        a5, b5 = sorted((1100 * sign, 1200 * sign))
        box("EastGate_%s_SideB" % tag, 3700, 4300, a5, b5, 0, H, M_WALL, "Alcoves")
        a6, b6 = sorted((600 * sign, 1200 * sign))
        box("EastGate_%s_Back" % tag, 4300, 4400, a6, b6, 0, H, M_WALL, "Alcoves")
        box("EastGate_%s_Roof" % tag, 3650, 4400, a6, b6, 400, 460, M_WALL, "Alcoves")
        spawn_point("EastGates", 3950, 820 * sign, 180.0)
        spawn_point("EastGates", 3950, 980 * sign, 180.0)
        light("EastGate_%s_Light" % tag, 4000, 900 * sign, 330, 5000.0, 900.0)

    # --------------------------------------------------------------- flow: exit gate + runners on the main floor axis
    k.exit_gate(3600, half_opening=350, height=1500)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_West", 600, 1360)
    k.runner("Runner_East", 2840, 3600)
    k.exit_passage(3600, EXIT_X, height=700)

    out += k.finish(NAME, "DA_Room_Arena04_CisternTiers", "Cistern Tiers", unreal.FPSRLArenaType.VERTICAL, EXIT_X,
                    room_center=(2200, 0, 350), room_extent=(2300, 2300, 900), nav_center=(2200, 0, 300), nav_extent=(2400, 2400, 800),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
