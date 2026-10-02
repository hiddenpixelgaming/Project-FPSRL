"""Creates the chat input assets (re-runnable): IA_OpenChat (digital) and IMC_Chat mapping it to T and gamepad D-pad Left.
Rebind the keys in IMC_Chat; the code only knows the action (UFPSRLChatSettings.OpenChatAction / ChatMappingContext)."""
import unreal

out = []
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary


def get_or_create(path, name, cls, factory):
    full = "%s/%s" % (path, name)
    if lib.does_asset_exist(full):
        return lib.load_asset(full)
    return tools.create_asset(name, path, cls, factory)


try:
    action = get_or_create("/Game/Input/Actions", "IA_OpenChat", unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    action.set_editor_property("action_description", "Open the session text chat")
    lib.save_loaded_asset(action)

    context = get_or_create("/Game/Input", "IMC_Chat", unreal.InputMappingContext, unreal.InputMappingContext_Factory())
    context.unmap_all()
    for key_name in ("T", "Gamepad_DPad_Left"):
        key = unreal.Key()
        key.set_editor_property("key_name", key_name)
        context.map_key(action, key)
    lib.save_loaded_asset(context)
    out.append("chat input: IA_OpenChat + IMC_Chat (T, Gamepad_DPad_Left) saved")
except Exception as ex:
    import traceback
    out.append("FAILED %s %s" % (ex, traceback.format_exc()))
for line in out:
    unreal.log_warning("CHATINPUT " + line)
