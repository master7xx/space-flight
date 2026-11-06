"""Import the reproducible WAV sources as Unreal SoundWave assets."""
import os
import unreal

project = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
source_dir = os.path.join(project, "Art", "Audio")
names = (
    "engine", "launch", "laser", "impact", "explosion_small", "explosion_medium",
    "explosion_large", "isotope", "upgrade", "denied", "tree_open", "tree_close",
    "mode_on", "mode_off", "pause", "resume", "ui_tick", "stress",
)
names += tuple(f"{name}_{variant}" for name in (
    "laser", "impact", "explosion_small", "explosion_medium", "explosion_large") for variant in (2, 3))
tasks = []
for name in names:
    task = unreal.AssetImportTask()
    task.filename = os.path.join(source_dir, name + ".wav")
    task.destination_path = "/Game/Audio"
    task.destination_name = "S_" + name
    task.automated = True
    task.save = True
    task.replace_existing = True
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for name, task in zip(names, tasks):
    if not task.imported_object_paths:
        raise RuntimeError("Could not import sound: " + name)
    sound = unreal.load_asset(task.imported_object_paths[0])
    if not sound:
        raise RuntimeError("Could not load imported sound: " + name)
    if name == "engine":
        sound.set_editor_property("looping", True)
        unreal.EditorAssetLibrary.save_loaded_asset(sound)
    unreal.log("Imported sound " + name + ": " + task.imported_object_paths[0])
