"""Arena 05 - Ember Causeway (Hazard, 35 x 30 m).

The hall floor is a dark hazard pool (it only hurts while you stand in it). Stone paths 40 cm above it: an entry
platform, a central island linked to the entry and exit platforms by narrow axis bridges, two side causeways, and two
cover islands against the walls that can only be reached across a strip of hazard (or a stepping stone). Enemies arrive
on the exit side and must come along the stone (their navigation avoids the pool); players can shortcut or flank through
the pool if they accept the damage. The brazier column on the central island is the landmark (off the axis).
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, spawn_point, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_05_EmberCauseway"
EXIT_X = 4500.0
H = 800
POOL = -40.0     # hazard surface: under the 45 cm step height, so players can wade in and out; no lights over it (it must read as black)
out = []
try:
    k.begin(NAME)
    k.vestibule(height=H)
    m_hazard = k.hazard_material()

    # --------------------------------------------------------------- the pool and its hazard logic
    box("HazardPool", 600, 4100, -1500, 1500, POOL - 40, POOL, m_hazard, "Hazard")
    k.hazard_zone("HazardZone", 600, 4100, -1500, 1500, POOL, damage_per_second=10.0)

    # --------------------------------------------------------------- stone (top 0)
    def stone(label, x0, x1, y0, y1):
        box(label, x0, x1, y0, y1, POOL - 40, 0, M_FLOOR, "Stone")
    stone("EntryPlatform", 600, 1300, -700, 700)
    stone("AxisBridge_W", 1300, 1800, -150, 150)
    stone("CentralIsland", 1800, 2800, -450, 450)
    stone("AxisBridge_E", 2800, 3300, -150, 150)
    stone("ExitPlatform", 3300, 4100, -1500, 1500)
    stone("Causeway_N", 1000, 3300, 700, 1000)
    stone("Causeway_S", 1000, 3300, -1000, -700)
    stone("CoverIsland_N", 1900, 2700, 1150, 1500)
    stone("CoverIsland_S", 1900, 2700, -1500, -1150)
    stone("SteppingStone_N", 1950, 2130, 525, 625)
    stone("SteppingStone_S", 2470, 2650, -625, -525)

    # Open elevation (user rule, 2026-10-01): a raised stone platform 1.5 m up on each side causeway, steps at both ends,
    # open edges (a drop to the causeway or the pool), a pillar to move around. High ground over the pool.
    P_Z = 150.0
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        y0, y1 = sorted((700 * sign, 1000 * sign))
        box("Platform_%s" % tag, 1900, 2600, y0, y1, POOL - 40, P_Z, M_FLOOR, "Platforms")
        for s in range(1, 7):   # 6 steps of 25 cm, 30 cm deep
            box("Platform_%s_StepW_%d" % (tag, s), 1900 - 30 * (7 - s), 1900 - 30 * (6 - s), y0, y1, POOL - 40, 25.0 * s, M_FLOOR, "Platforms")
            box("Platform_%s_StepE_%d" % (tag, s), 2600 + 30 * (6 - s), 2600 + 30 * (7 - s), y0, y1, POOL - 40, 25.0 * s, M_FLOOR, "Platforms")
        cyl("Platform_%s_Pillar" % tag, 2250, 850 * sign, P_Z, 130, 320, M_FEATURE, "Platforms")

    # --------------------------------------------------------------- landmark and cover
    cyl("Brazier_Column", 2300, 300, 0, 260, 900, M_FEATURE, "Landmark")
    cyl("Brazier_Bowl", 2300, 300, 900, 420, 70, M_FEATURE, "Landmark")
    light("Brazier_Light", 2300, 300, 1060, 18000.0, 2400.0)
    box("Island_Altar", 2150, 2450, -420, -160, 0, 120, M_FEATURE, "Cover")
    cyl("Island_Stub_SW", 1950, -330, 0, 140, 200, M_FEATURE, "Cover")
    cyl("Island_Stub_NE", 2680, 330, 0, 140, 160, M_FEATURE, "Cover")
    box("Entry_Wall_N", 1150, 1200, 250, 650, 0, 110, M_WALL, "Cover")
    box("Entry_Wall_S", 1150, 1200, -650, -250, 0, 110, M_WALL, "Cover")
    box("CoverIsland_N_Block", 2100, 2500, 1265, 1385, 0, 120, M_FEATURE, "Cover")
    box("CoverIsland_S_Block", 2100, 2500, -1385, -1265, 0, 120, M_FEATURE, "Cover")
    box("Exit_Block_N", 3450, 3750, 550, 850, 0, 120, M_FEATURE, "Cover")
    box("Exit_Block_S", 3450, 3750, -850, -550, 0, 120, M_FEATURE, "Cover")

    # --------------------------------------------------------------- walls, gates and far doors (all on the exit side)
    box("Wall_W_N", 500, 600, 400, 1600, 0, H, M_WALL, "Walls")
    box("Wall_W_S", 500, 600, -1600, -400, 0, H, M_WALL, "Walls")
    box("Wall_W_Lintel", 500, 600, -400, 400, 500, H, M_WALL, "Walls")
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        a, b = sorted((1500 * sign, 1600 * sign))
        box("Wall_%s_1" % tag, 500, 3500, a, b, POOL - 40, H, M_WALL, "Walls")
        box("Wall_%s_2" % tag, 3900, 4200, a, b, 0, H, M_WALL, "Walls")
        box("Wall_%s_Gate_Lintel" % tag, 3500, 3900, a, b, 450, H, M_WALL, "Walls")
        zone = "NorthGate" if sign > 0 else "SouthGate"
        c, d = sorted((1500 * sign, 2200 * sign))
        box("%s_Floor" % zone, 3500, 3900, c, d, -40, 0, M_FLOOR, "Alcoves")
        e, f = sorted((1600 * sign, 2200 * sign))
        box("%s_SideA" % zone, 3400, 3500, e, f, 0, H, M_WALL, "Alcoves")
        box("%s_SideB" % zone, 3900, 4000, e, f, 0, H, M_WALL, "Alcoves")
        g, h = sorted((2200 * sign, 2300 * sign))
        box("%s_Back" % zone, 3400, 4000, g, h, 0, H, M_WALL, "Alcoves")
        i0, i1 = sorted((1550 * sign, 2300 * sign))
        box("%s_Roof" % zone, 3400, 4000, i0, i1, H, H + 60, M_WALL, "Alcoves")
        yaw = -90.0 if sign > 0 else 90.0
        spawn_point(zone, 3610, 1800 * sign, yaw)
        spawn_point(zone, 3790, 1800 * sign, yaw)
        spawn_point(zone, 3700, 2000 * sign, yaw)
        light("%s_Light" % zone, 3700, 1900 * sign, 560, 6000.0, 1000.0)
        # Far doors beside the exit.
        for y0, y1 in ((1300, 1500), (350, 900)):
            a2, b2 = sorted((y0 * sign, y1 * sign))
            box("Wall_E_%s_%d" % (tag, y0), 4100, 4200, a2, b2, 0, H, M_WALL, "Walls")
        a3, b3 = sorted((900 * sign, 1300 * sign))
        box("Wall_E_Door_%s_Lintel" % tag, 4100, 4200, a3, b3, 450, H, M_WALL, "Walls")
        box("FarDoor_%s_Floor" % tag, 4100, 4800, a3, b3, -40, 0, M_FLOOR, "Alcoves")
        a4, b4 = sorted((800 * sign, 900 * sign))
        box("FarDoor_%s_SideA" % tag, 4200, 4800, a4, b4, 0, H, M_WALL, "Alcoves")
        a5, b5 = sorted((1300 * sign, 1400 * sign))
        box("FarDoor_%s_SideB" % tag, 4200, 4800, a5, b5, 0, H, M_WALL, "Alcoves")
        a6, b6 = sorted((800 * sign, 1400 * sign))
        box("FarDoor_%s_Back" % tag, 4800, 4900, a6, b6, 0, H, M_WALL, "Alcoves")
        box("FarDoor_%s_Roof" % tag, 4150, 4900, a6, b6, H, H + 60, M_WALL, "Alcoves")
        spawn_point("FarDoors", 4450, 1020 * sign, 180.0)
        spawn_point("FarDoors", 4450, 1180 * sign, 180.0)
        light("FarDoor_%s_Light" % tag, 4500, 1100 * sign, 560, 6000.0, 1000.0)

    # --------------------------------------------------------------- flow: exit gate + runner along the stone axis
    k.exit_gate(4100, half_opening=350, height=1350)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_Axis", 600, 4100)
    k.exit_passage(4100, EXIT_X, height=H)

    out += k.finish(NAME, "DA_Room_Arena05_EmberCauseway", "Ember Causeway", unreal.FPSRLArenaType.HAZARD, EXIT_X,
                    room_center=(2350, 0, 350), room_extent=(2500, 2300, 800), nav_center=(2400, 0, 250), nav_extent=(2600, 2400, 600),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
