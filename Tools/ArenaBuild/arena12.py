"""Arena 12 - Collapsed Atrium (Ruin, 36 x 32 m). In the Depth 1-3 rotation.

Abyssus reference: collapsed temple sections and rubble, carved pillars next to fallen ones, shallow pools; the dome
rooms read as places where something fell in. Our open-elevation rule: height without walled-off areas.
Design: a domed atrium whose upper floor collapsed. What is left of the mezzanine runs along the north side 3 m up; its
broken floor slabs lie as ramps down to the atrium floor at both ends (enemies climb them). The south half is a sunken
shallow pool (20 cm, walkable) where the dome came down, with dome fragments as tilted cover slabs. A toppled column lies
diagonally across the middle (long cover you can fight along), standing and broken pillars of every height around it.
One jump pad on the pool's edge throws a player up onto the mezzanine. Enemies come from an upper door onto the
mezzanine, a south hall beyond the pool and the doors beside the exit.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_12_CollapsedAtrium"
HALL_END, EXIT_X, HALF, H = 4200.0, 4600.0, 1600.0, 900.0
MEZ = 300.0
out = []
try:
    k.begin(NAME)
    k.vestibule(height=700)

    # Floor; the sunken pool (south, 20 cm down, dark).
    box("Floor_W", 600, 1800, -HALF, HALF, -40, 0, M_FLOOR, "Floor")
    box("Floor_E", 3300, HALL_END, -HALF, HALF, -40, 0, M_FLOOR, "Floor")
    box("Floor_N", 1800, 3300, -500, HALF, -40, 0, M_FLOOR, "Floor")
    box("Pool", 1800, 3300, -HALF, -500, -60, -20, M_FEATURE, "Floor")

    # Mezzanine remains (north, 3 m up) and its fallen slabs as ramps at both ends.
    box("Mezzanine", 2000, 3500, 750, HALF, MEZ - 60, MEZ, M_FLOOR, "Mezzanine")
    for x in (2200, 2800, 3400):
        cyl("Mezzanine_Pillar_%d" % x, x, 790, 0, 100, MEZ - 60, M_WALL, "Mezzanine")
    k.ramp("FallenSlab_W", 1450, 2000, 850, 1350, 0, MEZ, M_FEATURE, "Mezzanine")
    k.ramp("FallenSlab_E", 3500, 4000, 1100, 1550, MEZ, 0, M_FEATURE, "Mezzanine")
    box("Mezzanine_Cover", 2950, 3200, 1000, 1120, MEZ, MEZ + 110, M_FEATURE, "Mezzanine")

    # The toppled column across the middle (long cover), its base and capital.
    boxc("Toppled_Column", 2550, 50, 90, 1500, 180, 180, M_FEATURE, "Landmark", yaw=22.0)
    cyl("Column_Base", 1850, -230, 0, 320, 160, M_FEATURE, "Landmark")
    boxc("Column_Capital", 3270, 330, 120, 300, 300, 240, M_FEATURE, "Landmark", yaw=40.0, pitch=8.0)
    light("Atrium_Light", 2550, 0, 1100, 14000.0, 2600.0)

    # Dome fragments in the pool (tilted slabs), pillars of every height.
    boxc("DomeFragment_1", 2150, -1050, 60, 500, 260, 60, M_FEATURE, "Cover", yaw=15.0, roll=25.0)
    boxc("DomeFragment_2", 2950, -850, 70, 420, 240, 60, M_FEATURE, "Cover", yaw=-30.0, roll=-30.0)
    for i, (x, y, h) in enumerate(((1100, 700, 700), (1100, -700, 260), (3800, 700, 520), (3800, -700, 700),
                                   (2550, -1300, 140), (1500, -1250, 450), (3600, -1250, 300))):
        cyl("Pillar_%d" % i, x, y, 0, 170, h, M_WALL, "Pillars")
    box("Entry_Block", 1000, 1200, -200, 150, 0, 120, M_FEATURE, "Cover")

    # One jump pad: pool edge -> mezzanine (vz 850, ~1.25 s).
    k.jump_pad("JumpPad_Mezzanine", 2550, -350, 0, 0, 1080, 850)

    k.shell(HALL_END, HALF, H, side_doors=((2900, 3300, 1, MEZ, MEZ + 400), (2400, 2800, -1, 0, 450)))
    k.side_room("Mezzanine", 2900, 3300, 1, HALF, H, floor_z=MEZ)
    k.side_room("SouthHall", 2400, 2800, -1, HALF, H)

    k.exit_gate(HALL_END, half_opening=350, height=1500)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_W", 600, 1800)
    k.runner("Runner_E", 3300, HALL_END)
    k.exit_passage(HALL_END, EXIT_X, height=700)

    out += k.finish(NAME, "DA_Room_Arena12_CollapsedAtrium", "Collapsed Atrium", unreal.FPSRLArenaType.COVER, EXIT_X,
                    room_center=(2400, 0, 450), room_extent=(2000, 2400, 900), nav_center=(2350, 0, 300), nav_extent=(2700, 2500, 800),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
