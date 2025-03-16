file(GLOB INCLUDES include/*.h)
file(GLOB INCLUDES extern/*.h)
include_directories(SYSTEM ${OPENGL_INCLUDE_DIR})
include_directories(SYSTEM extern)
include_directories(SYSTEM include)

configure_file("${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h.in" "${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h")
add_executable (NavKit
  NavKit.cpp
  NavKit.rc
  adapter/RecastAdapter.cpp
  extern/RecastDemo/ChunkyTriMesh.cpp
  extern/RecastDemo/InputGeom.cpp
  extern/RecastDemo/MeshLoaderObj.cpp
  extern/RecastDemo/PerfTimer.cpp
  extern/RecastDemo/Sample.cpp
  extern/RecastDemo/SampleInterfaces.cpp
  extern/RecastDemo/Sample_SoloMesh.cpp
  extern/RecastDemo/imgui.cpp
  extern/RecastDemo/imguiRenderGL.cpp
  extern/easywsclient/easywsclient.cpp
  model/PfBoxes.cpp
  model/ReasoningGrid.cpp
  model/VisionData.cpp
  module/Airg.cpp
  module/GameConnection.cpp
  module/Gui.cpp
  module/InputHandler.cpp
  module/Logger.cpp
  module/Navp.cpp
  module/Obj.cpp
  module/Renderer.cpp
  module/SceneExtract.cpp
  module/Settings.cpp
  util/ErrorHandler.cpp
  util/FileUtil.cpp
  util/ErrorHandler.cpp
  util/Math.cpp
  module/Grid.cpp
  ../include/NavKit/module/Grid.h
)
target_include_directories(NavKit PUBLIC "${PROJECT_BINARY_DIR}")
target_include_directories(NavKit PRIVATE ${SIMPLEINI_INCLUDE_DIRS})
target_link_libraries(NavKit PRIVATE ${OPENGL_LIBRARIES} ${GLUT_LIBRARY} )
target_link_libraries(NavKit PRIVATE OpenGL::GLU)
target_link_libraries(NavKit PRIVATE OpenGL::GL)
target_link_libraries(NavKit PRIVATE GLUT::GLUT)
target_link_libraries(NavKit PRIVATE GLEW::GLEW)
target_link_libraries(NavKit PRIVATE cpptrace::cpptrace)
target_link_libraries(NavKit PRIVATE simdjson::simdjson)
target_link_libraries(NavKit PRIVATE RecastNavigation::Recast)
target_link_libraries(NavKit PRIVATE RecastNavigation::DebugUtils)
target_link_libraries(NavKit PRIVATE RecastNavigation::Detour)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourCrowd)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourTileCache)
target_link_libraries(NavKit PRIVATE ResourceLib_HM3)
target_link_libraries(NavKit PRIVATE ftgl)

add_library(nfd::nfd SHARED IMPORTED)
add_library(NavWeakness SHARED IMPORTED)
add_library(ResourceLib_HM3 SHARED IMPORTED)
target_link_libraries(NavKit PRIVATE nfd::nfd)
target_link_libraries(NavKit PRIVATE NavWeakness)
target_link_libraries(NavKit PRIVATE ResourceLib_HM3)

file(COPY "${NavKit_SOURCE_DIR}/src/resource/DroidSans.ttf" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(NAV_WEAKNESS_DLL "${NavKit_SOURCE_DIR}/lib/Debug/NavWeakness.dll")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(NAV_WEAKNESS_DLL "${NavKit_SOURCE_DIR}/lib/Release/NavWeakness.dll")
endif()
add_custom_command(
  TARGET NavKit
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NAV_WEAKNESS_DLL}" "${CMAKE_CURRENT_BINARY_DIR}/"
  COMMENT "Copying ${NAV_WEAKNESS_DLL} to ${CMAKE_BINARY_DIR}"
)

file(COPY "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/Glacier2Obj.exe" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/src/resource/Glacier2Obj.py" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/bin/Editor.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/lib/Debug/nfd.dll" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${NavKit_SOURCE_DIR}/src/resource/NavKit.ini" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

set_target_properties(NavWeakness PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/NavWeakness.dll"
  IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/NavWeakness.lib"
  IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/NavWeakness.lib"
  IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/NavWeakness.dll"
  IMPORTED_LOCATION_RELEASE "${NavKit_SOURCE_DIR}/lib/Release/NavWeakness.dll"
)
set_target_properties(nfd::nfd PROPERTIES
  NFD_OVERRIDE_RECENT_WITH_DEFAULT ON
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/nfd.dll"
  IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/nfd.lib"
  IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Debug/nfd.lib"
  IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/nfd.dll"
  IMPORTED_LOCATION_RELEASE "${NavKit_SOURCE_DIR}/lib/Debug/nfd.dll"
)
set_target_properties(ResourceLib_HM3 PROPERTIES
  IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/ResourceLib_HM3.dll"
  IMPORTED_IMPLIB_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.lib"
  IMPORTED_IMPLIB_RELEASE "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.lib"
  IMPORTED_LOCATION_DEBUG "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.dll"
  IMPORTED_LOCATION_RELEASE "${NavKit_SOURCE_DIR}/lib/Debug/ResourceLib_HM3.dll"
)

target_link_libraries(NavKit
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
set_target_properties(NavKit PROPERTIES WIN32_EXECUTABLE TRUE)

set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
