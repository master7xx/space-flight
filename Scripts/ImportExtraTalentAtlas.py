"""Import an additive talent atlas without overwriting existing UI assets."""

import os
import unreal

project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
source = os.path.join(project, "Art", "Generated", "TalentAtlasSciFi_v2_extra.png")
asset_path = "/Game/UI/T_TalentAtlasExtra"
if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
    raise RuntimeError("Refusing to overwrite an existing talent atlas: " + asset_path)
if not os.path.isfile(source):
    raise RuntimeError("Talent atlas image is missing: " + source)

task = unreal.AssetImportTask()
task.filename = source
task.destination_path = "/Game/UI"
task.destination_name = "T_TalentAtlasExtra"
task.automated = True
task.save = False
task.replace_existing = False
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
if not task.imported_object_paths:
    raise RuntimeError("Could not import the talent atlas: " + source)
texture = unreal.EditorAssetLibrary.load_asset(asset_path)
if not texture:
    raise RuntimeError("Imported talent atlas could not be loaded")
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
texture.set_editor_property("never_stream", True)
if not unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False):
    raise RuntimeError("Could not save the talent atlas asset")
unreal.log("EXTRA_TALENT_ATLAS_IMPORTED " + asset_path)
