set(HEADER_LIST 
    "${NavKit_SOURCE_DIR}/extern/fastlz/fastlz.h"
    "${NavKit_SOURCE_DIR}/extern/simdjson/simdjson.h"
    "${NavKit_SOURCE_DIR}/extern/simdjson/simdjson_helpers.h"
    "${NavKit_SOURCE_DIR}/include/NavKit/NavKit.h"
    "${NavKit_SOURCE_DIR}/include/NavWeakness/NavPower.h"
    "${NavKit_SOURCE_DIR}/include/NavWeakness/NavWeakness.h"
    "${NavKit_SOURCE_DIR}/include/NavWeakness/Vec3.h"
)
configure_file("${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h.in" "${NavKit_SOURCE_DIR}/include/NavKit/NavKitConfig.h")

add_executable (NavKit "NavKit.cpp"  "${NavKit_SOURCE_DIR}/extern/fastlz/fastlz.c" )
target_include_directories(NavKit PUBLIC "${PROJECT_BINARY_DIR}")
# target_link_libraries(NavKit ${OPENGL_LIBRARIES} ${GLUT_LIBRARY} )
target_link_libraries(NavKit PRIVATE OpenGL::GL)
target_link_libraries(NavKit PRIVATE RecastNavigation::Recast)
target_link_libraries(NavKit PRIVATE RecastNavigation::DebugUtils)
target_link_libraries(NavKit PRIVATE RecastNavigation::Detour)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourCrowd)
target_link_libraries(NavKit PRIVATE RecastNavigation::DetourTileCache)

#TARGET_LINK_LIBRARIES(NavKit PRIVATE NavWeakness::NavWeakness)

#target_link_libraries(NavKit PRIVATE "D:\\workspace\\NavKit\\External\\NavWeakness\\NavWeakness.lib")

target_link_libraries(NavKit
    PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET NavKit PROPERTY CXX_STANDARD 20)
endif()
