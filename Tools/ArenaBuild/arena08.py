"""Arena 08 - Wreck Chasm (Void, 35 x 30 m). Not in the Depth rotation yet.

Abyssus reference: shipwreck wood (masts, planks, hulls) used as walkways in the Abandoned Temple, and the Void: dark
chasms that cost a little health and put you back on safe ground (here: our fall response, -5%, back at the room's
entrance). Diagonal jump pads that launch you forward (Sanctuary) are the fast way across.
Design: most of the hall is a bottomless chasm. A wrecked ship lies across the middle (its deck is the central island,
low gunwales for cover, a leaning mast as the landmark). Three ways across, each with a different risk: the plank bridge
on the axis (fast, exposed), the fallen mast (a narrow diagonal beam north) and a wide stone bridge along the south wall
(safe, long, pillars for cover). Two diagonal jump pads throw a player over the void: from the entry ledge onto the deck,
and from the deck to the exit ledge. Enemies arrive on the exit ledge (gates in both side walls, doors beside the exit)
and must come across the same routes.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_08_WreckChasm"
HALL_END, EXIT_X, HALF, H = 4100.0, 4500.0, 1500.0, 800.0
out = []
try:
    k.begin(NAME)
    k.vestibule(height=700)

    # Ledges (deep, so their faces read as cliffs), the chasm walls below the side walls.
    box("Ledge_Entry", 600, 1300, -HALF, HALF, -900, 0, M_FLOOR, "Ledges")
    box("Ledge_Exit", 3400, HALL_END, -HALF, HALF, -900, 0, M_FLOOR, "Ledges")
    for sign in (1, -1):
        a, b = sorted((HALF * sign, (HALF + 100) * sign))
        box("ChasmWall_%s" % ("N" if sign > 0 else "S"), 1300, 3400, a, b, -1400, 0, M_WALL, "Ledges")

    # The wreck: hull (deep, dark) with its deck as the central island, gunwales, a leaning mast.
    box("Hull", 1800, 2900, -550, 550, -700, -20, M_FEATURE, "Wreck")
    box("Deck", 1800, 2900, -550, 550, -40, 0, M_FLOOR, "Wreck")
    for sign in (1, -1):
        tag = "N" if sign > 0 else "S"
        for i, (x0, x1) in enumerate(((1800, 2150), (2450, 2900))):
            a, b = sorted((520 * sign, 560 * sign))
            box("Gunwale_%s_%d" % (tag, i), x0, x1, a, b, 0, 110, M_FEATURE, "Wreck")
    boxc("Mast", 2350, 300, 600, 130, 130, 1200, M_FEATURE, "Landmark", roll=8.0)
    cyl("CrowsNest", 2350, 380, 1080, 300, 60, M_FEATURE, "Landmark")
    light("Mast_Light", 2350, 380, 1250, 12000.0, 2200.0)
    box("Deck_Crate_1", 2000, 2200, -380, -180, 0, 120, M_FEATURE, "Cover")
    box("Deck_Crate_2", 2550, 2700, 120, 300, 0, 100, M_FEATURE, "Cover")
    cyl("Capstan", 2650, -300, 0, 140, 110, M_FEATURE, "Cover")

    # Crossings: plank bridges on the axis, the fallen mast (north, diagonal), the stone bridge (south).
    box("Plank_W", 1300, 1800, -150, 150, -20, 0, M_FEATURE, "Bridges")
    box("Plank_E", 2900, 3400, -150, 150, -20, 0, M_FEATURE, "Bridges")
    boxc("FallenMast_W", 1550, 650, -10, 707, 150, 20, M_FEATURE, "Bridges", yaw=-45.0)
    boxc("FallenMast_E", 3150, 650, -10, 707, 150, 20, M_FEATURE, "Bridges", yaw=45.0)
    box("StoneBridge", 1300, 3400, -1300, -950, -300, 0, M_FLOOR, "Bridges")
    for x in (1800, 2350, 2900):
        cyl("StoneBridge_Pillar_%d" % x, x, -1010, 0, 120, 500, M_WALL, "Bridges")
    box("StoneBridge_Block", 2500, 2700, -1250, -1000, 0, 110, M_FEATURE, "Bridges")

    # Cover on the ledges.
    box("Entry_Wall_N", 1100, 1150, 300, 700, 0, 110, M_WALL, "Cover")
    box("Entry_Wall_S", 1100, 1150, -700, -300, 0, 110, M_WALL, "Cover")
    box("Exit_Block_N", 3550, 3800, 400, 650, 0, 120, M_FEATURE, "Cover")
    box("Exit_Block_S", 3550, 3800, -650, -400, 0, 120, M_FEATURE, "Cover")

    # Diagonal jump pads over the void (flight ~1.4 s at vz 700).
    k.jump_pad("JumpPad_EntryToDeck", 1150, -750, 0, 560, 300, 700)
    k.jump_pad("JumpPad_DeckToExit", 2750, 400, 0, 600, 250, 700)

    # Outer walls; gates on the exit ledge (both sides), doors beside the exit.
    k.shell(HALL_END, HALF, H, side_doors=((3550, 3950, 1, 0, 450), (3550, 3950, -1, 0, 450)))
    k.side_room("NorthGate", 3550, 3950, 1, HALF, H)
    k.side_room("SouthGate", 3550, 3950, -1, HALF, H)

    k.exit_gate(HALL_END, half_opening=350, height=1400)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_Entry", 600, 1300)
    k.runner("Runner_Deck", 1800, 2900)
    k.runner("Runner_Exit", 3400, HALL_END)
    k.exit_passage(HALL_END, EXIT_X, height=700)

    out += k.finish(NAME, "DA_Room_Arena08_WreckChasm", "Wreck Chasm", unreal.FPSRLArenaType.HAZARD, EXIT_X,
                    room_center=(2350, 0, 350), room_extent=(2000, 2300, 900), nav_center=(2300, 0, 0), nav_extent=(2650, 2400, 1000),
                    enemy_count=6, active_zones=2, depths=())
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
