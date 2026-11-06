import os
import unreal

project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
source = os.path.join(project, "Art", "Generated", "HudFrame.png")
task = unreal.AssetImportTask()
task.filename = source
task.destination_path = "/Game/UI"
task.destination_name = "T_HudFrame"
task.automated = True
task.save = True
task.replace_existing = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
if not task.imported_object_paths:
    raise RuntimeError("Could not import the HUD frame: " + source)
unreal.log("Imported HUD frame: " + ", ".join(task.imported_object_paths))
