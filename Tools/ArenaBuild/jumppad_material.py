"""Creates MI_JumpPad_Cyan (re-runnable): a copy of the Healing Altar's material instance with a bright cyan colour, used
by AFPSRLJumpPad's disc and arrow so pads read clearly against the grey arenas."""
import unreal

out = []
lib = unreal.EditorAssetLibrary
SRC = "/Game/MainProject/Contents/Materials/Debug/MI_HealingAltar_Lime"
DST = "/Game/MainProject/Contents/Materials/Debug/MI_JumpPad_Cyan"
try:
    if not lib.does_asset_exist(DST):
        lib.duplicate_asset(SRC, DST)
    mi = lib.load_asset(DST)
    names = [str(n) for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(mi)]
    out.append("vector params: %s" % names)
    for name in names:
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mi, name, unreal.LinearColor(0.05, 0.85, 1.0, 1.0))
    lib.save_loaded_asset(mi)
    out.append("saved %s" % DST)
except Exception as ex:
    import traceback
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("PADMAT " + line)
