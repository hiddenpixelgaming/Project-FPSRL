"""Arena 07 - Drowned Nave (Temple, two levels, 36 x 30 m). In the Depth 1-3 rotation.

Abyssus reference: the Abandoned Temple's rooms (stone ruins, shallow pools, braziers, collapsed sections) and the
reviewers' "tight, vertical arenas with ledges above and low ground below": height is how you escape a swarm.
Design: a temple nave. The floor is a sunken shallow basin (20 cm, walkable, dark) around a stepped altar dais with two
brazier pillars (landmark, the axis between them stays open to the exit gate). Open galleries 3.5 m up run along both
side walls on pillars: stairs at the entrance end, a collapsed-rubble ramp at the far end (enemies use both), and two jump
pads on the basin edges that throw a player straight up onto them (the fast way up). Under the galleries is a covered
walkway. Enemies come from the apse doors beside the exit, an upper door onto the north gallery and a hall under the
south gallery, so they arrive both high and low.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_07_DrownedNave"
HALL_END, EXIT_X, HALF, H = 4200.0, 4600.0, 1500.0, 900.0
GAL = 350.0
out = []
try:
    k.begin(NAME)
    k.vestibule(height=700)

    # Floors: the basin (X 1500..3300, Y +-650) sits 20 cm down, dark (shallow water in greyscale).
    box("Floor_W", 600, 1500, -HALF, HALF, -40, 0, M_FLOOR, "Floor")
    box("Floor_E", 3300, HALL_END, -HALF, HALF, -40, 0, M_FLOOR, "Floor")
    box("Floor_N", 1500, 3300, 650, HALF, -40, 0, M_FLOOR, "Floor")
    box("Floor_S", 1500, 3300, -HALF, -650, -40, 0, M_FLOOR, "Floor")
    box("Basin", 1500, 3300, -650, 650, -60, -20, M_FEATURE, "Floor")

    # The altar dais: three 40 cm tiers, two brazier pillars flanking the axis.
    for i, (hx, hy, top) in enumerate(((450, 450, 20), (350, 350, 60), (250, 250, 100))):
        box("Dais_%d" % i, 2400 - hx, 2400 + hx, -hy, hy, -20, top, M_FEATURE, "Landmark")
    for sign in (1, -1):
        cyl("Brazier_%s" % ("N" if sign > 0 else "S"), 2400, 330 * sign, 100, 130, 750, M_FEATURE, "Landmark")
        cyl("Brazier_%s_Bowl" % ("N" if sign > 0 else "S"), 2400, 330 * sign, 850, 260, 50, M_FEATURE, "Landmark")
        light("Brazier_%s_Light" % ("N" if sign > 0 else "S"), 2400, 330 * sign, 960, 9000.0, 1600.0)

    # Basin cover: broken low walls and fallen column drums.
    box("Basin_Wall_NW", 1700, 2000, 380, 440, -20, 100, M_WALL, "Cover")
    box("Basin_Wall_SE", 2800, 3100, -440, -380, -20, 100, M_WALL, "Cover")
    cyl("Drum_1", 1850, -380, -20, 160, 90, M_FEATURE, "Cover")
    cyl("Drum_2", 2950, 400, -20, 160, 90, M_FEATURE, "Cover")
    box("Entry_Block_N", 1000, 1200, 500, 800, 0, 120, M_FEATURE, "Cover")
    box("Entry_Block_S", 1000, 1200, -800, -500, 0, 120, M_FEATURE, "Cover")
    box("Apse_Block_N", 3600, 3850, 350, 650, 0, 120, M_FEATURE, "Cover")
    box("Apse_Block_S", 3600, 3850, -650, -350, 0, 120, M_FEATURE, "Cover")

    # Galleries (both sides): deck 3.5 m up on pillars, open edge; stairs (west), rubble ramp (east), a jump pad.
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        y0, y1 = sorted((1050 * sign, HALF * sign))
        box("Gallery_%s_Deck" % tag, 1200, 3500, y0, y1, GAL - 40, GAL, M_FLOOR, "Galleries")
        for x in (1500, 2100, 2700, 3300):
            cyl("Gallery_%s_Pillar_%d" % (tag, x), x, 1090 * sign, 0, 90, GAL - 40, M_WALL, "Galleries")
        s0, s1 = sorted((1150 * sign, 1450 * sign))
        for s in range(1, 15):   # 14 x 25 cm, 30 cm deep
            box("Gallery_%s_Stair_%d" % (tag, s), 1200 - 30 * (15 - s), 1200 - 30 * (14 - s), s0, s1, 0, 25.0 * s, M_FLOOR, "Galleries")
        r0, r1 = sorted((1250 * sign, HALF * sign))
        k.ramp("Gallery_%s_Rubble" % tag, 3500, 3950, r0, r1, GAL, 0, M_FEATURE, "Galleries")
        box("Gallery_%s_Cover" % tag, 2700, 2900, 1200 * sign - 60, 1200 * sign + 60, GAL, GAL + 110, M_FEATURE, "Galleries")
        k.jump_pad("JumpPad_%s" % tag, 2400, 850 * sign, 0, 0, 260 * sign, 940)

    # Outer walls, an upper door onto the north gallery, a hall under the south gallery, doors beside the exit.
    k.shell(HALL_END, HALF, H, side_doors=((2800, 3200, 1, GAL, GAL + 400), (2000, 2400, -1, 0, 300)))
    k.side_room("NorthGallery", 2800, 3200, 1, HALF, H, floor_z=GAL)
    k.side_room("SouthHall", 2000, 2400, -1, HALF, H)

    # Flow: exit gate, runner along the axis (down into the basin and up again), between the braziers.
    k.exit_gate(HALL_END, half_opening=350, height=1400)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_W", 600, 1500)
    k.runner("Runner_Basin", 1500, 1950, z=-20)
    k.runner("Runner_E", 3300, HALL_END)
    k.exit_passage(HALL_END, EXIT_X, height=700)

    out += k.finish(NAME, "DA_Room_Arena07_DrownedNave", "Drowned Nave", unreal.FPSRLArenaType.VERTICAL, EXIT_X,
                    room_center=(2400, 0, 450), room_extent=(2000, 2300, 900), nav_center=(2350, 0, 300), nav_extent=(2700, 2400, 800),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
