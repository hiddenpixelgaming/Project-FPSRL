"""Arena 02 - Ruined Colonnade (Cover / Obstruction, 35 x 30 m).

A ruined pillared hall: four rows of massive pillars carrying beams 8 m up, broken and fallen pieces, a collapsed
interior wall, a fallen colossal head. Cover is the architecture. A clear central aisle with overhead cross-beams and a
floor runner points at the exit gate (flow rule). Enemies arrive from a north door, a south door, two far doors beside
the exit and a crypt stairwell rising out of the floor.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, spawn_point, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_02_RuinedColonnade"
EXIT_X = 4500.0
H = 800
out = []
try:
    k.begin(NAME)
    k.vestibule(height=H)

    # Floor around the crypt opening (X 2600..3200, Y 1150..1450).
    box("Floor_West", 600, 2600, -1500, 1500, -40, 0, M_FLOOR, "Floor")
    box("Floor_East", 3200, 4100, -1500, 1500, -40, 0, M_FLOOR, "Floor")
    box("Floor_Mid_S", 2600, 3200, -1500, 1150, -40, 0, M_FLOOR, "Floor")
    box("Floor_Mid_N", 2600, 3200, 1450, 1500, -40, 0, M_FLOOR, "Floor")

    # Crypt: stairs down to a lit chamber 2 m below the floor; enemies climb out toward the hall.
    box("Crypt_Floor", 2600, 3240, 1110, 1490, -240, -200, M_FLOOR, "Crypt")
    for s in range(1, 9):
        box("Crypt_Step_%d" % s, 2600 + 40 * (s - 1), 2600 + 40 * s, 1150, 1450, -240, -25.0 * s, M_FLOOR, "Crypt")
    box("Crypt_Wall_S", 2600, 3240, 1110, 1150, -240, -40, M_WALL, "Crypt")
    box("Crypt_Wall_N", 2600, 3240, 1450, 1490, -240, -40, M_WALL, "Crypt")
    box("Crypt_Wall_E", 3200, 3240, 1110, 1490, -240, -40, M_WALL, "Crypt")
    box("Crypt_Parapet", 2900, 3200, 1130, 1150, 0, 90, M_FEATURE, "Crypt")
    for i, (x, y) in enumerate(((3050, 1230), (3130, 1370), (2970, 1370))):
        spawn_point("Crypt", x, y, 180.0, floor_z=-200.0)
    light("Crypt_Light", 2980, 1300, -60, 9000.0, 800.0)

    # Colonnade: 4 rows x 5 columns, leaving a clear central aisle (Y -450..450) that points at the exit.
    broken = {(1800, 450): 140, (3000, 1050): 280}
    skip = {(2400, -450), (3600, -1050)}
    for x in (1200, 1800, 2400, 3000, 3600):
        for y in (-1050, -450, 450, 1050):
            if (x, y) in skip:
                continue
            h = broken.get((x, y), H)
            box("Pillar_%d_%d" % (x, y), x - 80, x + 80, y - 80, y + 80, 0, h, M_WALL, "Colonnade")
    # The fallen pillar (stump + shaft lying across the south half of the aisle) and the rubble of the last one.
    box("Pillar_2400_-450_Stump", 2320, 2480, -530, -370, 0, 60, M_WALL, "Colonnade")
    boxc("Pillar_Fallen", 2650, -300, 80, 560, 160, 160, M_FEATURE, "Cover", yaw=38.7)
    boxc("Rubble_3600", 3600, -1050, 45, 260, 220, 90, M_FEATURE, "Cover", yaw=12)
    # Beams on the pillars (broken where the pillars are), and cross-beams over the aisle framing the way to the exit.
    box("Beam_N_Outer", 1100, 2950, 1000, 1100, H, H + 100, M_WALL, "Beams")
    box("Beam_N_Inner", 1100, 3700, 400, 500, H, H + 100, M_WALL, "Beams")
    box("Beam_S_Inner", 1100, 2300, -500, -400, H, H + 100, M_WALL, "Beams")
    box("Beam_S_Outer", 1100, 3500, -1100, -1000, H, H + 100, M_WALL, "Beams")
    for x in (1200, 2400, 3600):
        box("CrossBeam_%d" % x, x - 60, x + 60, -1100, 1100, H + 100, H + 180, M_WALL, "Beams")

    # Cover in the aisles: a sarcophagus block, the collapsed interior wall (south), the fallen colossal head (landmark).
    box("Sarcophagus", 1820, 2180, 190, 370, 0, 110, M_FEATURE, "Cover")
    # Rubble of the collapsed interior wall: waist high (open sightlines; user rule: no walled-off areas).
    box("CollapsedWall_1", 1500, 1950, -790, -710, 0, 130, M_FEATURE, "Cover")
    box("CollapsedWall_2", 2150, 2500, -790, -710, 0, 90, M_FEATURE, "Cover")
    box("CollapsedWall_3", 3100, 3500, -790, -710, 0, 130, M_FEATURE, "Cover")

    # Open elevation: a raised walkway 3 m up between the two north pillar rows (the fallen upper floor), stairs at both
    # ends, open edges. High ground over the aisle, the pillars to move around up there.
    W_Z = 300.0
    box("Walkway_Deck", 1200, 2600, 540, 960, 0, W_Z, M_FLOOR, "Walkway")
    for s in range(1, 13):   # 12 steps of 25 cm, 25 cm deep
        box("Walkway_StairW_%d" % s, 1200 - 25 * (13 - s), 1200 - 25 * (12 - s), 600, 900, 0, 25.0 * s, M_FLOOR, "Walkway")
        box("Walkway_StairE_%d" % s, 2600 + 25 * (12 - s), 2600 + 25 * (13 - s), 600, 900, 0, 25.0 * s, M_FLOOR, "Walkway")
    box("Walkway_Block", 1750, 1950, 640, 860, W_Z, W_Z + 110, M_FEATURE, "Walkway")
    boxc("Colossus_Head", 950, -1180, 170, 380, 380, 380, M_FEATURE, "Landmark", yaw=30, pitch=20)
    boxc("Colossus_Crown", 820, -1260, 330, 140, 300, 120, M_FEATURE, "Landmark", yaw=30, pitch=35)

    # Outer walls.
    box("Wall_W_N", 500, 600, 400, 1500, 0, H, M_WALL, "Walls")
    box("Wall_W_S", 500, 600, -1500, -400, 0, H, M_WALL, "Walls")
    box("Wall_W_Lintel", 500, 600, -400, 400, 500, H, M_WALL, "Walls")
    for sign in (1, -1):
        for y0, y1 in ((1300, 1500), (350, 900)):
            a, b = sorted((y0 * sign, y1 * sign))
            box("Wall_E_%d_%d" % (sign, y0), 4100, 4200, a, b, 0, H, M_WALL, "Walls")
        a, b = sorted((900 * sign, 1300 * sign))
        box("Wall_E_FarDoor_Lintel_%d" % sign, 4100, 4200, a, b, 450, H, M_WALL, "Walls")
    box("Wall_N_1", 500, 1600, 1500, 1600, 0, H, M_WALL, "Walls")
    box("Wall_N_2", 2000, 4200, 1500, 1600, 0, H, M_WALL, "Walls")
    box("Wall_N_Door_Lintel", 1600, 2000, 1500, 1600, 450, H, M_WALL, "Walls")
    box("Wall_S_1", 500, 2600, -1600, -1500, 0, H, M_WALL, "Walls")
    box("Wall_S_2", 3000, 4200, -1600, -1500, 0, H, M_WALL, "Walls")
    box("Wall_S_Door_Lintel", 2600, 3000, -1600, -1500, 450, H, M_WALL, "Walls")

    # Side doors (north, south) with lit alcoves.
    for zone, x0, x1, sign in (("NorthDoor", 1600, 2000, 1), ("SouthDoor", 2600, 3000, -1)):
        a, b = sorted((1500 * sign, 2200 * sign))
        box("%s_Floor" % zone, x0, x1, a, b, -40, 0, M_FLOOR, "Alcoves")
        a, b = sorted((1600 * sign, 2200 * sign))
        box("%s_SideA" % zone, x0 - 100, x0, a, b, 0, H, M_WALL, "Alcoves")
        box("%s_SideB" % zone, x1, x1 + 100, a, b, 0, H, M_WALL, "Alcoves")
        a, b = sorted((2200 * sign, 2300 * sign))
        box("%s_Back" % zone, x0 - 100, x1 + 100, a, b, 0, H, M_WALL, "Alcoves")
        a, b = sorted((1550 * sign, 2300 * sign))
        box("%s_Roof" % zone, x0 - 100, x1 + 100, a, b, H, H + 60, M_WALL, "Alcoves")
        cx = (x0 + x1) / 2.0
        yaw = -90.0 if sign > 0 else 90.0
        spawn_point(zone, cx - 90, 1800 * sign, yaw)
        spawn_point(zone, cx + 90, 1800 * sign, yaw)
        spawn_point(zone, cx, 2000 * sign, yaw)
        light("%s_Light" % zone, cx, 1900 * sign, 560, 6000.0, 1100.0)

    # Far doors beside the exit gate.
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        a, b = sorted((900 * sign, 1300 * sign))
        box("FarDoor_%s_Floor" % tag, 4100, 4800, a, b, -40, 0, M_FLOOR, "Alcoves")
        a2, b2 = sorted((800 * sign, 900 * sign))
        box("FarDoor_%s_SideA" % tag, 4200, 4800, a2, b2, 0, H, M_WALL, "Alcoves")
        a3, b3 = sorted((1300 * sign, 1400 * sign))
        box("FarDoor_%s_SideB" % tag, 4200, 4800, a3, b3, 0, H, M_WALL, "Alcoves")
        a4, b4 = sorted((800 * sign, 1400 * sign))
        box("FarDoor_%s_Back" % tag, 4800, 4900, a4, b4, 0, H, M_WALL, "Alcoves")
        box("FarDoor_%s_Roof" % tag, 4150, 4900, a4, b4, H, H + 60, M_WALL, "Alcoves")
        spawn_point("FarDoors", 4450, 1020 * sign, 180.0)
        spawn_point("FarDoors", 4450, 1180 * sign, 180.0)
        light("FarDoor_%s_Light" % tag, 4500, 1100 * sign, 560, 6000.0, 1000.0)

    # Flow: exit gate + runner down the central aisle.
    k.exit_gate(4100, half_opening=350, height=1350)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_Aisle", 600, 4100)
    k.exit_passage(4100, EXIT_X, height=H)

    out += k.finish(NAME, "DA_Room_Arena02_RuinedColonnade", "Ruined Colonnade", unreal.FPSRLArenaType.COVER, EXIT_X,
                    room_center=(2350, 0, 400), room_extent=(2500, 2300, 800), nav_center=(2400, 0, 250), nav_extent=(2550, 2400, 700),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
