"""Light for the template rooms (Traversal, Miniboss, Exit, Final Boss arena, Prep): they have no lights of their own
(only the entry map's sun and sky), so they went black in packaged builds, then still read too dark next to the sunlit
arenas (playtests v0.1.25, v0.1.28). Each gets a grid of soft movable fill lights (no shadows) over its floor plan and a
post process volume around it that brightens the view (exposure compensation) while a player is inside. Re-runnable:
removes what it placed before. Run: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>."""
import unreal
sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ROOMS = ["LV_Z1_Traversal_01", "LV_Z1_Exit_01", "LV_Z1_BossArena_01", "LV_Z1_Prep_01"]  # LV_Z1_Miniboss_01 is a full arena now (miniboss_juggernaut.py lights it)
LIGHT_INTENSITY = 3000.0
LIGHT_RADIUS = 1800.0
SPACING = 800.0
EXPOSURE_BIAS = 1.0     # +1 stop while inside the room
EXPOSURE_BIAS_PER_ROOM = {"LV_Z1_BossArena_01": 1.8}   # read darkest in the packaged check
for name in ROOMS:
    path = "/Game/MainProject/Contents/Rooms/Zone1/" + name
    world = unreal.EditorLoadingAndSavingUtils.load_map(path)
    removed = 0
    lo = [1e9, 1e9, 1e9]; hi = [-1e9, -1e9, -1e9]
    for a in sub.get_all_level_actors():
        if a.get_actor_label().startswith(("FillLight_", "RoomExposure")):
            sub.destroy_actor(a); removed += 1; continue
        if a.get_class().get_name() in ("StaticMeshActor", "Brush") or "Geometry" in a.get_actor_label():
            o, e = a.get_actor_bounds(False)
            if max(e.x, e.y, e.z) > 20000: continue
            for i, c in enumerate("xyz"):
                lo[i] = min(lo[i], getattr(o, c) - getattr(e, c)); hi[i] = max(hi[i], getattr(o, c) + getattr(e, c))
    sx, sy = hi[0] - lo[0], hi[1] - lo[1]
    nx, ny = max(1, int(round(sx / SPACING))), max(1, int(round(sy / SPACING)))
    z = lo[2] + min(500.0, (hi[2] - lo[2]) * 0.6)
    count = 0
    for ix in range(nx):
        for iy in range(ny):
            x = lo[0] + sx * (ix + 0.5) / nx; y = lo[1] + sy * (iy + 0.5) / ny
            l = sub.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z), unreal.Rotator())
            l.set_actor_label("FillLight_%d_%d" % (ix, iy))
            c = l.point_light_component
            c.set_mobility(unreal.ComponentMobility.MOVABLE)
            c.set_editor_property("intensity", LIGHT_INTENSITY)
            c.set_editor_property("attenuation_radius", LIGHT_RADIUS)
            c.set_editor_property("cast_shadows", False)
            l.set_folder_path("Lighting/Fill")
            count += 1
    # Brighter view while inside: a bounded post process volume over the room (the brush is a 200 unit cube).
    center = unreal.Vector((lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2)
    vol = sub.spawn_actor_from_class(unreal.PostProcessVolume, center, unreal.Rotator())
    vol.set_actor_label("RoomExposure")
    vol.set_actor_scale3d(unreal.Vector(sx / 200.0, sy / 200.0, max(400.0, hi[2] - lo[2]) / 200.0))
    vol.set_editor_property("unbound", False)
    vol.set_editor_property("blend_radius", 200.0)
    settings = vol.get_editor_property("settings")
    settings.set_editor_property("override_auto_exposure_bias", True)
    bias = EXPOSURE_BIAS_PER_ROOM.get(name, EXPOSURE_BIAS)
    settings.set_editor_property("auto_exposure_bias", bias)
    vol.set_editor_property("settings", settings)
    vol.set_folder_path("Lighting")
    unreal.EditorLoadingAndSavingUtils.save_map(world, path)
    unreal.log_warning("FL %s: %d fill lights, exposure +%.1f volume (%d old removed)" % (name, count, bias, removed))
