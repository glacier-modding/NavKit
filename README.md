# NavKit
*A tool to generate Navp and Airg files for Hitman: World of Assassination Scenes in the Glacier 2 Engine*
# Running instructions
You can run NavKit by opening `NavKit.exe`. To use the **View Navp in game** and **Scene Extraction** features, you will need ZHMModSdk installed, and you will need to copy the included `Editor.dll` to your `Hitman 3\Retail\mods` folder. For the **Scene Extraction** feature, you will also need to have Blender installed.

You will also need to install the latest Visual C++ Redistributable from https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-140. Make sure you get the X64 version.

Functions available are loading and saving Navp and Navp.json, loading Airg and Airg.json, loading and saving Obj, scene extraction, building Navp, building Airg and viewing navp in game.  

# View Navp in game
1. Open Hitman and launch a mission.
1. Open NavKit and load a Navp file for that mission.
1. Click the `Send Navp` button in NavKit.

# Full generation of Navp and Airg (Scene Extraction, Navp generation, Airg generation)
1. Download the lastest release of NavKit (https://github.com/glacier-modding/NavKit/releases/latest) and extract it to any folder (e.g. C:\NavKit)
1. Install ZHMModSDK (https://github.com/OrfeasZ/ZHMModSDK/releases/latest)
1. Backup the "HITMAN 3\Retail\mods\Editor.dll" file
1. Copy the included Editor.dll from the NavKit folder to the "HITMAN 3\Retail\mods" folder
1. Install Blender (tested with 3.4 https://download.blender.org/release/Blender3.4/)
1. Install the latest Visual C++ Redistributable from https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-140. Make sure you get the X64 version.
1. Start Hitman and enter a mission
1. Open NavKit
1. On the "Extract menu" of NavKit, set your file paths:
    1. Hitman 3 directory (e.g. C:\Program Files (x86)\Steam\steamapps\common\HITMAN 3)
    1. Output directory (e.g. C:\NavKit\Output)
    1. Blender Executable (e.g. C:\Program Files\Blender Foundation\Blender 3.4\blender.exe)
1. On the "Extract menu" of NavKit, click "Extract from game". It may take up to around 10 minutes depending on the complexity of the mission being extracted, and whether the alocs have already been extracted from the rpkg files.  
1. When the OBJ finishes generating, you can save it to an OBJ file so you can load it later if you'd like by clicking the "Save Obj" button on the "Obj menu"
1. On the "Navp menu" click the "Build Navp from Obj" button
1. On the "Navp menu" click the "Save Navp" button and choose where to save the navp file
1. On the "Airg menu" click the "Build Airg from Navp" button
1. On the "Airg menu" click the "Save Airg" button and choose where to save the airg file

# Adding navp and airg to a new scene
1. Include the .navp and .airg files in the chunk folder for your brick
1. Choose an IOI string for it (for instance [assembly:/_pro/scenes/missions/trapped/scene_wolverine_modified.navp].pc_navp)
1. In GlacierKit, search the original scene file for "navp" and it should take you to the Pathfinder Configuration (usually at Scene\Scenario_[scene name]\Global\Pathfinder\PathfinderConfiguration
1. Copy the entity id (in this example screenshot it is 0f374e8c1e033807) and put it somewhere easily accessible
1. Open your new brick file in GlacierKit in the Editor
1. In the Property overrides for your new brick, add this to add the navp:
```
    {
        "entities": [
            {
                "ref": "[pathfinder entity id]",
                "externalScene": "[original scene file]"
            }
        ],
        "properties": {
                "m_NavpowerResourceID": {
                    "type": "ZRuntimeResourceID",
                    "value": {
                        "resource": "[new navp ioi string]",
                        "flag": "5F"
                    }
                }
        }
    }
```
7. Where [pathfinder entity id] is the id you copied earlier for the pathfinder,  [original scene file] is the ioi string of the original scene (e.g. [assembly:/_pro/scenes/missions/snug/mission_vanilla.brick].pc_entitytype), and [new navp ioi string] is your new ioi string for the navp.
1. In GlacierKit, search the original scene file for "airg" and it should take you to the Pathfinder Configuration (usually at Scene\Scenario_[scene name]\Global\Pathfinder\ReasoningGridConfig
1. Copy the entity id (in the other example screenshot it is 6c6e6c3217bf058e) and put it somewhere easily accessible
1. In the Property overrides for your new brick, add this to add the airg:
```
    {
        "entities": [
            {
                "ref": "[reasoning grid entity id]",
                "externalScene": "[original scene file]"
            }
        ],
        "properties": {
                "m_pGrid": {
                    "type": "ZRuntimeResourceID",
                    "value": {
                        "resource": "[new airg ioi string]",
                        "flag": "5F"
                    }
                }
        }
    }
```
11. Where [reasoning grid entity id] is the id you copied earlier for the pathfinder,  [original scene file] is the ioi string of the original scene (e.g. [assembly:/_pro/scenes/missions/snug/mission_vanilla.brick].pc_entitytype), and [new airg ioi string] is your new ioi string for the navp.
1. Save your brick file
1. In GlacierKit, under the Text Tools, paste your navp ioi string into the Hash Calculator
1. Copy the Hex value, and rename your navp to [hex value].navp Where [hex value] is the Hex value you calculated
1. Paste your airg ioi string into the Hash Calculator
1. Copy the Hex value, and rename your airg to [hex value].airg Where [hex value] is the Hex value you calculated
1. Deploy the mod

# How NavKit generates Navp and Airg files 
NavKit performs the following series of steps to be able to generate Navp files.
1. Connect to the Editor server of the running Hitman game, and issue commands to: rebuild the entity tree, find the scene's ZGeomEntities, PFBox entities, and PFSeedPoint entities and send their data back to NavKit, where they are saved to `output.nav.json` in the specified output folder.
1. Extract all the needed Aloc files from the rpkg files to the `aloc` folder of the specified output folder.
1. Open the blender cli and run `glacier2obj.py` to generate an obj by importing all of the Aloc files, copy them the number if times they are used in the scene, and transform each one according to what was sent by the game, and save it to `output.obj` in the specified output folder.
1. Load `output.obj` from the specified output folder or another specified Obj file.
1. At this point, the build Navp section of the menu will be available, and you can customize the parameters, then press build to call Recast to generate the Navmesh. Then you can save the Navmesh as a Navp or Navp.json file by pressing the Save Navp button.
1. At this point, the build Airg section of the menu will be available, and you can customize the parameters, then press build to generate the Airg.
# Disclaimer
*NavKit is still a work in progress, and there may be glitches or issues with Obj, Navp, or Airg generation in the current version. If you encounter any problems while running NavKit please create an issue on this GitHub repo.*
# Future enhancements
* Faster Scene generation
* Linux and MacOS support
# Building instructions (CLion or Visual Studio)
You can also build NavKit yourself. To build NavKit:
1. Clone this repository with the '--recurse-submodules' option
1. Open in Visual Studio or CLion
1. Cmake should load
1. In the x64 command prompt make a build folder, cd into it, and run:  
`cmake -B . -S ..`
1. Then change back to the main directory and run  
`cmake --preset x64-debug`
# Building instructions (Rider)
See [Rider Instructions](docs/rider_instructions.md)
# Credits
2kpr  
Anthony Fuller  
Atampy25  
Dafitius  
Dribbleondo  
Invalid  
IOI  
Jojje  
Kercyx  
Kevin Rudd  
LaNombre  
Luka  
Lyssa  
NoFate  
Notex  
Pavle  
Piepieonline  
Rdil  
Recast team  
Voodoo Hillbilly  
And everyone at the Glacier 2 discord!
