"""Arena 10 - Soul Canal (Lanes, 40 x 24 m). In the Depth 1-3 rotation.

Abyssus reference: the Sanctuary: catacomb chambers with burial niches, streams of hostile souls that hurt, and frequent
diagonal jump pads. Reviewers liked hazards that change how you move through a room more than hazards that are scenery.
Design: a long catacomb crossed by three soul streams (orange lanes, 2 m wide, 30 cm down: they hurt only while you
stand in them; a running jump clears them). Each has one stone crossing in the middle (the enemies' way over, a natural
chokepoint). Along the north wall an open catwalk 3 m up runs nearly the whole length on pillars, above the streams: the
safe, exposed route. Stairs at both ends; two diagonal jump pads on the south floor launch a player up onto it mid-room.
The south side is a colonnade of burial niches (pillars and sarcophagi) for cover. Enemies come from the doors beside
the exit, a crypt hall in the south wall and an upper door onto the catwalk, so the catwalk is contested.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_10_SoulCanal"
HALL_END, EXIT_X, HALF, H = 4600.0, 5000.0, 1200.0, 900.0
CAT = 300.0
STREAMS = (1500.0, 2700.0, 3900.0)     # each stream's start X (2 m wide)
out = []
try:
    k.begin(NAME)
    k.vestibule(height=700)
    m_hazard = k.hazard_material()

    # Floor in pieces between the streams; the streams; their crossings.
    edges = [600.0] + [x for s in STREAMS for x in (s, s + 200)] + [HALL_END]
    for i in range(0, len(edges), 2):
        box("Floor_%d" % i, edges[i], edges[i + 1], -HALF, HALF, -40, 0, M_FLOOR, "Floor")
    for i, s in enumerate(STREAMS):
        box("Stream_%d" % i, s, s + 200, -HALF, HALF, -70, -30, m_hazard, "Hazard")
        k.hazard_zone("StreamZone_%d" % i, s, s + 200, -HALF, HALF, -30, damage_per_second=10.0)
        box("Crossing_%d" % i, s, s + 200, -90, 90, -70, 0, M_FLOOR, "Crossings")

    # Catwalk along the north wall, on pillars; stairs at the west end (along +X) and the east end (along -Y).
    box("Catwalk", 1040, 4300, 780, HALF, CAT - 40, CAT, M_FLOOR, "Catwalk")
    for x in range(1300, 4300, 600):
        cyl("Catwalk_Pillar_%d" % x, x, 820, 0, 90, CAT - 40, M_WALL, "Catwalk")
    k.stairs("Catwalk_StairW", 620, 975, 0, CAT, "+x", 350, M_FLOOR, "Catwalk", step_d=35.0)
    k.stairs("Catwalk_StairE", 4200, 330, 0, CAT, "+y", 200, M_FLOOR, "Catwalk", step_d=37.5)
    for i, x in enumerate((2400, 3000)):
        box("Catwalk_Cover_%d" % i, x - 100, x + 100, 1000, 1120, CAT, CAT + 110, M_FEATURE, "Catwalk")

    # South colonnade: burial niches (pillar pairs, sarcophagi), a reliquary column as the landmark.
    for x in range(1000, 4500, 600):
        cyl("Niche_Pillar_%d" % x, x, -820, 0, 150, 650, M_WALL, "Niches")
    for i, x in enumerate((1250, 2450, 3650)):
        box("Sarcophagus_%d" % i, x - 150, x + 150, -1000, -850, 0, 100, M_FEATURE, "Niches")
    cyl("Reliquary", 3300, -450, 0, 300, 1000, M_FEATURE, "Landmark")
    cyl("Reliquary_Cap", 3300, -450, 1000, 450, 60, M_FEATURE, "Landmark")
    light("Reliquary_Light", 3300, -450, 1150, 12000.0, 2200.0)
    box("Mid_Block_1", 2100, 2300, 200, 450, 0, 120, M_FEATURE, "Cover")
    box("Mid_Block_2", 4150, 4350, -400, -150, 0, 120, M_FEATURE, "Cover")
    box("Entry_Wall_S", 1100, 1150, -650, -250, 0, 110, M_WALL, "Cover")

    # Diagonal jump pads up onto the catwalk (vz 900, ~1.4 s).
    k.jump_pad("JumpPad_1", 1950, -350, 0, 0, 900, 900)
    k.jump_pad("JumpPad_2", 3500, 150, 0, 0, 600, 900)

    k.shell(HALL_END, HALF, H, side_doors=((3300, 3700, 1, CAT, CAT + 400), (2900, 3300, -1, 0, 450)))
    k.side_room("Catwalk", 3300, 3700, 1, HALF, H, floor_z=CAT)
    k.side_room("Crypt", 2900, 3300, -1, HALF, H)

    k.exit_gate(HALL_END, half_opening=350, height=1500)
    k.runner("Runner_Vestibule", 50, 600)
    for i in range(0, len(edges), 2):
        k.runner("Runner_%d" % i, edges[i], edges[i + 1])
    k.exit_passage(HALL_END, EXIT_X, height=700)

    out += k.finish(NAME, "DA_Room_Arena10_SoulCanal", "Soul Canal", unreal.FPSRLArenaType.CHOKEPOINT, EXIT_X,
                    room_center=(2600, 0, 450), room_extent=(2300, 2000, 900), nav_center=(2550, 0, 300), nav_extent=(3000, 2100, 800),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
