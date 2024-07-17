# NavKit
*A tool to generate Navp files for Hitman: World of Assassination Scenes in the Glacier 2 Engine*
# Running instructions
You can run NavKit by opening `NavKit.exe`. To use the **View Navp in game** and **Scene Extraction** features, you will need ZHMModSdk installed, and you will need to copy the included `Editor.dll` to your `Hitman 3\Retail\mods` folder. For the **Scene Extraction** feature, you will also need to have Blender installed.

Functions available are loading and saving Navp and Navp.json, loading Airg and Airg.json, loading and saving Obj, scene extraction, building Navp, and viewing navp in game.  
# View Navp in game
1. Open Hitman and launch a mission.
1. Open NavKit and load a Navp file for that mission.
1. Click the `Send Navp` button in NavKit.
# Scene Extraction
1. Open Hitman and start the mission you wish to extract.
1. Open NavKit and click the Open Extract Menu button.
1. Set your Hitman folder, Output directory, and Blender .exe file.
1. Press the Extract button.  

It may take anywhere from 1 minute to upwards of 30 minutes depending on the complexity of the mission being extracted, and whether the prims have already been extracted from the rpkg files.  
# How NavKit generates Navp files 
NavKit performs the following series of steps to be able to generate Navp files.
1. Run `glacier2obj.exe` to connect to the Editor server of the running Hitman game, and issue commands to: rebuild the entity tree, find the scene's brick tblu hashes, and send them back to NavKit.
1. Get the temp hashes for those bricks, and scan those bricks' dependencies to get the hashes for all Prim dependencies that are children of ALOCs and save them to `toFind.json` in the specified output folder.
1. Connect to the Editor server of the running Hitman game again, and issue a command with all of the ZGeomEntity / Prim hashes to get the transforms of all the ZGeomEntities and send them back to NavKit and save them to `prims.json` in the specified output folder.
1. Extract all of the needed Prim files from the rpkg files to the `prim` folder of the specified output folder.
1. Open the blender cli and run `glacier2obj.py` to generate an obj by importing all of the Prim files, copy them the number if times they are used in the scene, and transform each one according to what was sent by the game, and save it to `output.obj` in the specified output folder.
1. Load `output.obj` from the specified output folder in NavKit. You can also save the obj file to another filename by pressing the Save obj button.
1. At this point, the build Navp section of the menu will be available and you can customize the parameters, then press build to call Recast to generate the Navmesh. Then you can save the Navmesh as a Navp or Navp.json file by pressing the Save Navp button.
# Disclaimer
*NavKit is still a work in progress, and there may be glitches or issues with navp generation in the current version. If you encounter any problems while running NavKit please create an issue on this GitHub repo.*
# Future enhancements
* Airg Generation
* Faster Scene generation
* Linux and MacOs support
# Building instructions
You can also build NavKit yourself. To build NavKit:
1. Clone this repository with the '--recurse-submodules' option
1. Open in Visual Studio
1. Cmake should load
1. In the x64 command prompt make a build folder, cd into it, and run:  
`cmake -B . -S ..`
1. Then change back to the main directory and run  
`cmake --preset x64-debug`
# Credits
NoFate  
Atampy25  
Notex  
Anthony Fuller  
Recast team  
rdil  
2kpr  
kercyx  
Kevin Rudd  
And everyone at the Glacier 2 discord!
