## Building and debugging NavKit with JetBrains Rider
1. Create a new run configuration of type Shell Script
   1. Set the name to `CMake init`
   2. Set the command type to "Script text" and set the script text to `New-Item -ItemType Directory -Path [NavKit root directory]\build -ErrorAction SilentlyContinue; cd build; cmake --preset x64-debug ..`
   3. Set the working directory to the NavKit root directory
2. Create another new run configuration of type Native Executable
   1. Set the name to `NavKit`
   2. Set the exe path to `[NavKit root directory]/build/x64-debug/Debug/NavKit.exe`
   3. Set the working directory to `[NavKit root directory]/build/x64-debug/Debug`
   4. Delete the existing entry in `Before Launch`
   5. Create a new entry in `Before Launch` of type `External Tool`
   6. Name it `CMake build NavKit Debug`
   7. Set the Program to `cmake`
   8. Set the Arguments to `--build --preset x64-debug --target NavKit`
   9. Set the Working Directory to `[NavKit Root Directory]`