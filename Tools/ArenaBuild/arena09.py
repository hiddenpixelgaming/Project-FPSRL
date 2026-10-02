"""Arena 09 - Hanging Gardens (King of the hill, 37 x 34 m). In the Depth 1-3 rotation.

Abyssus reference: the Gardens: overgrown ruins with large deep pools crossed on raised paths and stone bridges, and
wider rooms than the Temple. Design rule from the reviews: rooms need something to fight over, not only to look at.
Design: a garden plateau 3 m up in the middle of the hall, ringed by a moat of hazard pool (orange: it hurts, 10/s).
Four ramps bridge the moat onto the plateau (the axis ramp leads straight up from the entrance and down toward the exit);
two jump pads on the outer ring throw a player across the moat onto the plateau (a fast, committed way up). The plateau
holds a great tree (landmark, off the axis), planters for cover and open edges. The outer ring is the low ground: pillars
and planters, room to circle. Holding the plateau sees everything but is exposed from every side; enemies come from
gates in both side walls near the exit and the doors beside the exit, and climb the ramps to dislodge you.
"""
import os, sys, traceback
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import unreal
import arena_kit as k
from arena_kit import box, boxc, cyl, light, M_FLOOR, M_WALL, M_FEATURE

NAME = "LV_Arena_09_HangingGardens"
HALL_END, EXIT_X, HALF, H = 4300.0, 4700.0, 1700.0, 800.0
TOP = 300.0
M0, M1, MY = 1300.0, 3700.0, 1200.0      # moat outer rectangle
P0, P1, PY = 1700.0, 3300.0, 800.0       # plateau
out = []
try:
    k.begin(NAME)
    k.vestibule(height=700)
    m_hazard = k.hazard_material()

    # Outer ring floor around the moat, the moat (hazard), the plateau.
    box("Floor_W", 600, M0, -HALF, HALF, -40, 0, M_FLOOR, "Floor")
    box("Floor_E", M1, HALL_END, -HALF, HALF, -40, 0, M_FLOOR, "Floor")
    box("Floor_N", M0, M1, MY, HALF, -40, 0, M_FLOOR, "Floor")
    box("Floor_S", M0, M1, -HALF, -MY, -40, 0, M_FLOOR, "Floor")
    box("Moat", M0, M1, -MY, MY, -120, -40, m_hazard, "Hazard")
    k.hazard_zone("MoatZone", M0, M1, -MY, MY, -40, damage_per_second=10.0)
    box("Plateau", P0, P1, -PY, PY, -120, TOP, M_FLOOR, "Plateau")

    # Ramps over the moat (W and E on the axis, N and S in the middle of the sides).
    k.ramp("Ramp_W", M0, P0, -250, 250, 0, TOP, M_FEATURE, "Ramps")
    k.ramp("Ramp_E", P1, M1, -250, 250, TOP, 0, M_FEATURE, "Ramps")
    for sign in (1, -1):   # along Y: a slab pitched with roll
        import math
        run, rise = MY - PY, TOP
        length = math.hypot(run, rise)
        roll = math.degrees(math.atan2(rise, run)) * (1 if sign > 0 else -1)
        cy = (MY + PY) / 2.0 * sign
        boxc("Ramp_%s" % ("N" if sign > 0 else "S"), 2500, cy, TOP / 2.0 - 20, 500, length, 40, M_FEATURE, "Ramps", roll=roll)

    # On the plateau: the great tree (landmark), planters, a low wall.
    cyl("Tree_Trunk", 2700, 380, TOP, 260, 950, M_FEATURE, "Landmark")
    cyl("Tree_Crown", 2700, 380, TOP + 950, 1100, 90, M_FEATURE, "Landmark")
    light("Tree_Light", 2700, 380, TOP + 1150, 14000.0, 2400.0)
    box("Planter_1", 2250, 2550, -500, -300, TOP, TOP + 110, M_FEATURE, "Plateau")
    box("Planter_2", 2850, 3100, -650, -400, TOP, TOP + 110, M_FEATURE, "Plateau")
    box("Plateau_Wall", 2250, 2550, 450, 500, TOP, TOP + 120, M_WALL, "Plateau")

    # Outer ring: pillars and planters to circle around.
    for i, (x, y) in enumerate(((1000, 1350), (1000, -1350), (4000, 1350), (4000, -1350), (2500, 1500), (2500, -1500))):
        cyl("Ring_Pillar_%d" % i, x, y, 0, 180, 700, M_WALL, "Ring")
    for i, (x0, x1, y0, y1) in enumerate(((800, 1100, 500, 750), (800, 1100, -750, -500), (3850, 4100, 450, 700), (3850, 4100, -700, -450),
                                          (1600, 1900, 1350, 1500), (3100, 3400, -1500, -1350))):
        box("Ring_Planter_%d" % i, x0, x1, y0, y1, 0, 110, M_FEATURE, "Ring")

    # Jump pads across the moat onto the plateau (north and south, ~1.25 s flight).
    for sign in (1, -1):
        k.jump_pad("JumpPad_%s" % ("N" if sign > 0 else "S"), 2100, 1400 * sign, 0, 0, -730 * sign, 850)   # lands ~3 m in from the plateau edge

    k.shell(HALL_END, HALF, H, side_doors=((3700, 4100, 1, 0, 450), (3700, 4100, -1, 0, 450)))
    k.side_room("NorthGate", 3700, 4100, 1, HALF, H)
    k.side_room("SouthGate", 3700, 4100, -1, HALF, H)

    k.exit_gate(HALL_END, half_opening=350, height=1500)
    k.runner("Runner_Vestibule", 50, 600)
    k.runner("Runner_W", 600, M0)
    k.runner("Runner_Plateau", P0, P1, z=TOP)
    k.runner("Runner_E", M1, HALL_END)
    k.exit_passage(HALL_END, EXIT_X, height=700)

    out += k.finish(NAME, "DA_Room_Arena09_HangingGardens", "Hanging Gardens", unreal.FPSRLArenaType.OPEN, EXIT_X,
                    room_center=(2450, 0, 450), room_extent=(2100, 2500, 900), nav_center=(2400, 0, 300), nav_extent=(2800, 2600, 800),
                    enemy_count=6, active_zones=2)
except Exception as ex:
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("AR " + line)
