"""Arena 11 - Bell Spire (Tiers, 35 x 34 m). In the Depth 1-3 rotation.

Abyssus reference: the Submarine / bell objectives and the general "ledges above, low ground below" verticality; jump
pads as the fast way up. Reviewers: fights are better when there is a place everyone wants to stand.
Design: a stepped spire in the middle of a square hall: a wide first tier 2.5 m up (ramps on the axis from the entrance
side and down toward the exit, stairs north and south) and a small top tier 5 m up (stairs from the first tier), where a
bell hangs from a tall column (the landmark, on the north edge of the first tier so the axis view to the exit gate stays open). Two jump
pads on the floor: one launches straight up onto the top tier, one onto the first tier. Top tier: sees the whole hall,
no cover, every enemy can see you. First tier: low walls. Floor: pillar groups in the corners. Enemies come from gates in
both side walls (one near each end) and the doors beside the exit.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_11_BellSpire"
HALL_END, EXIT_X, HALF, H = 4100.0, 4500.0, 1700.0, 900.0
T1, T2 = 250.0, 500.0
CX = 2350.0
out = []
try:
    k.begin(NAME)
    k.vestibule(height=700)
    box("Floor", 600, HALL_END, -HALF, HALF, -40, 0, M_FLOOR, "Floor")

    # Tier 1 (14 x 14 m) with axis ramps (W up, E down) and stairs N and S; tier 2 (6 x 6 m).
    box("Tier1", CX - 700, CX + 700, -700, 700, -40, T1, M_FLOOR, "Spire")
    k.ramp("Tier1_Ramp_W", CX - 1100, CX - 700, -250, 250, 0, T1, M_FEATURE, "Spire")
    k.ramp("Tier1_Ramp_E", CX + 700, CX + 1100, -250, 250, T1, 0, M_FEATURE, "Spire")
    k.stairs("Tier1_Stair_N", CX - 300, 1000, 0, T1, "-y", 300, M_FLOOR, "Spire")
    k.stairs("Tier1_Stair_S", CX + 300, -1000, 0, T1, "+y", 300, M_FLOOR, "Spire")
    box("Tier2", CX - 300, CX + 300, -300, 300, T1, T2, M_FLOOR, "Spire")
    k.stairs("Tier2_Stair", CX - 175, 600, T1, T2, "-y", 250, M_FLOOR, "Spire")
    for i, (x0, x1, y0, y1) in enumerate(((CX - 650, CX - 450, 350, 600), (CX + 450, CX + 650, -600, -350), (CX + 350, CX + 650, 450, 500), (CX - 650, CX - 350, -500, -450))):
        box("Tier1_Wall_%d" % i, x0, x1, y0, y1, T1, T1 + 110, M_WALL, "Spire")

    # The bell column beside the top tier (landmark).
    cyl("BellColumn", CX + 150, 600, T1, 160, 1150, M_FEATURE, "Landmark")
    box("BellArm", CX + 90, CX + 210, 300, 660, T1 + 1050, T1 + 1150, M_FEATURE, "Landmark")
    cyl("Bell", CX + 150, 330, T1 + 820, 280, 230, M_FEATURE, "Landmark")
    light("Bell_Light", CX + 150, 330, T1 + 1300, 14000.0, 2600.0)

    # Floor: pillar groups in the four corners, low blocks.
    for i, (x, y) in enumerate(((1050, 1150), (1050, -1150), (3650, 1150), (3650, -1150))):
        for dx, dy in ((0, 0), (260, 0), (0, 260 if y < 0 else -260)):
            cyl("Corner_Pillar_%d_%d_%d" % (i, dx, dy), x + dx, y + dy, 0, 150, 600, M_WALL, "Pillars")
    box("Floor_Block_1", 1100, 1300, -200, 100, 0, 120, M_FEATURE, "Cover")
    box("Floor_Block_2", 3450, 3650, 50, 350, 0, 120, M_FEATURE, "Cover")

    # Jump pads: floor -> top tier (south), floor -> tier 1 (north).
    k.jump_pad("JumpPad_Top", CX, -1000, 0, 0, 480, 1100)
    k.jump_pad("JumpPad_Tier1", CX + 450, 1050, 0, 0, -330, 800)

    k.shell(HALL_END, HALF, H, side_doors=((1500, 1900, 1, 0, 450), (2800, 3200, -1, 0, 450)))
    k.side_room("NorthGate", 1500, 1900, 1, HALF, H)
    k.side_room("SouthGate", 2800, 3200, -1, HALF, H)

    k.exit_gate(HALL_END, half_opening=350, height=1600)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_W", 600, CX - 1100)
    k.runner("Runner_E", CX + 1100, HALL_END)
    k.exit_passage(HALL_END, EXIT_X, height=700)

    out += k.finish(NAME, "DA_Room_Arena11_BellSpire", "Bell Spire", unreal.FPSRLArenaType.VERTICAL, EXIT_X,
                    room_center=(2350, 0, 450), room_extent=(2000, 2500, 900), nav_center=(2300, 0, 300), nav_extent=(2650, 2600, 800),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
