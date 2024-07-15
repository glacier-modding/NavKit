# NavKit
A tool to generate NAVP files for Hitman: World of Assassination Scenes in the Glacier 2 Engine

# Running instructions

You can run NavKit by opening `NavKit.exe`.  

Functions available are loading and saving Navp and Navp.json, loading Airg and Airg.json, loading and saving Obj, generating Navp, and scene extraction.

# Scene extraction

To extract from scene you must have Blender and currently it must be installed to `C:\Program Files\Blender Foundation\Blender 3.4\blender.exe`. This will be customizable later.  
Click the Open Extract Menu button and set your Hitman folder and Output directory, then press the Build button. It may take anywhere from 1 minute to upwards of 30 minutes depending on the complexity of the mission being extracted, and whether the prims have already been extracted from the rpkg files.

# Future enhancements

* Airg Generation
* Faster Scene generation

You can also build NavKit yourself:

# Building instructions
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
