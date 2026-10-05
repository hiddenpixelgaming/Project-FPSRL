"""Materials for the enemy roles. Run with the editor closed:
UnrealEditor-Cmd FPSRL.uproject -run=pythonscript -script=<this file>
- M_EnemyAimLine: unlit red for the Marksman's aim line and the Brute's shockwave ring (UFPSRLEnemyRoleComponent,
  AFPSRLShockwave). Pure red: brighter turns orange through the tonemapper, and orange is the hazard colour.
- M_EnemyPhased: see-through cyan glow laid over a phased Skirmisher (overlay material)."""
import unreal

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
FOLDER = "/Game/MainProject/Contents/Materials/Enemies"


def material(name):
    path = FOLDER + "/" + name
    if lib.does_asset_exist(path):
        m = unreal.load_asset(path)
        mel.delete_all_material_expressions(m)
    else:
        m = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    return m


def constant(m, value, x, y):
    c = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, x, y)
    c.set_editor_property("constant", value)
    return c


line = material("M_EnemyAimLine")
mel.connect_material_property(constant(line, unreal.LinearColor(0.9, 0.0, 0.01, 1.0), -300, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(line)
lib.save_loaded_asset(line)

phased = material("M_EnemyPhased")
phased.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
phased.set_editor_property("used_with_skeletal_mesh", True)	# an overlay on the role body
mel.connect_material_property(constant(phased, unreal.LinearColor(0.15, 0.9, 1.3, 1.0), -300, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
opacity = mel.create_material_expression(phased, unreal.MaterialExpressionConstant, -300, 200)
opacity.set_editor_property("r", 0.45)
mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
mel.recompile_material(phased)
lib.save_loaded_asset(phased)
unreal.log("[EnemySetup] role materials in " + FOLDER)

# M_EnemyTelegraph: see-through red for ground warnings (Juggernaut missile marks, mine areas, its charge's blast area).
telegraph = material("M_EnemyTelegraph")
telegraph.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mel.connect_material_property(constant(telegraph, unreal.LinearColor(1.6, 0.0, 0.0, 1.0), -300, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
t_opacity = mel.create_material_expression(telegraph, unreal.MaterialExpressionConstant, -300, 200)
t_opacity.set_editor_property("r", 0.45)
mel.connect_material_property(t_opacity, "", unreal.MaterialProperty.MP_OPACITY)
mel.recompile_material(telegraph)
lib.save_loaded_asset(telegraph)

# M_PlatformHighlight: glowing cyan rim of a lit Juggernaut platform (cyan = "go here"; red = enemy, orange = hazard).
rim = material("M_PlatformHighlight")
mel.connect_material_property(constant(rim, unreal.LinearColor(0.2, 1.6, 2.2, 1.0), -300, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(rim)
lib.save_loaded_asset(rim)
unreal.log("[EnemySetup] telegraph and platform materials in " + FOLDER)
