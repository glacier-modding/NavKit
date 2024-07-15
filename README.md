# NavKit
A tool to generate NAVP files for Hitman: World of Assassination Scenes in the Glacier 2 Engine

# Running instructions

You can run NavKit by opening `NavKit.exe`.  

Functions available are loading and saving Navp and Navp.json, loading Airg and Airg.json, loading and saving Obj, generating Navp, and scene extraction.

Note: NavKit is still a work in progress, and there may be glitches or issues with navp generation in the current version. if your encounter any problems while running NavKit please create an issue on this GitHub repo.

# Scene extraction

To extract from scene you must have Blender installed and currently it must be installed to `C:\Program Files\Blender Foundation\Blender 3.4\blender.exe`. This will be customizable later.  
You will also need to compile the latest version of ZHModSDK from this branch (https://github.com/dbierek/ZHMModSDK/tree/Add-listPrimEntities) and copy the Editor.dll file to your retail/mods folder in your Hitman folder.  
Open Hitman and start the mission you wish to extract. In NavKit, click the Open Extract Menu button and set your Hitman folder and Output directory, then press the Build button. It may take anywhere from 1 minute to upwards of 30 minutes depending on the complexity of the mission being extracted, and whether the prims have already been extracted from the rpkg files.  

# How it works 

NavKit performs a series of steps to be able to generate Navp files. 

1. Connect to the Editor server of the running Hitman game, and issue commands to: rebuild the entity tree, and find the scene's brick tblu hashes and send them back to NavKit.  
1. Get the temp hashes for those bricks, and scan them to get the hashes for all prim dependencies that are children of geomentities and save it to 'toFind.json' in the output folder.  
1. Connect to the Editor server of the running Hitman game again, and issue a command with all of the geomentity / prim hashes to get the transforms of all the geomentities and send them back to NavKit and save them to 'prims.json' in the output folder.  
1. Extract all of the prim files from the rpkg files to the 'prim' folder of the output folder.  
1. Open the blender cli and run a command to generate an obj by importing all of the prim files, copy them the number if times they are used in the scene, and transform each one according to what was sent by the game, and save it to 'output.obj' in the NavKit folder.
1. Open that obj in NavKit. you can also save the obj file to another filename by pressing the Save obj button.  
1. At this point, the build Navp section of the menu will be available and you can customize the parameters, then press build to call Recast to generate the Navmesh. then you can save the Navmesh as a Navp or Navp.json file by pressing the Save Navp button.  

# Future enhancements

* Airg Generation
* Faster Scene generation
* Linux and MacOs support

You can also build NavKit yourself:

# Building instructions
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
