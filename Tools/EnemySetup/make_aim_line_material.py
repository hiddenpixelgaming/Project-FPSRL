"""M_EnemyAimLine: unlit, glowing red, for the Marksman's aim line (UFPSRLEnemyRoleComponent). Run with the editor
closed: UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>"""
import unreal

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
FOLDER = "/Game/MainProject/Contents/Materials/Enemies"
NAME = "M_EnemyAimLine"
path = FOLDER + "/" + NAME
if lib.does_asset_exist(path):
    material = unreal.load_asset(path)
    mel.delete_all_material_expressions(material)
else:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
colour = mel.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -300, 0)
colour.set_editor_property("constant", unreal.LinearColor(0.9, 0.0, 0.01, 1.0))	# pure red (brighter turns orange through the tonemapper: orange is the hazard colour)
mel.connect_material_property(colour, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(material)
lib.save_loaded_asset(material)
unreal.log("[EnemySetup] aim line material " + path)
