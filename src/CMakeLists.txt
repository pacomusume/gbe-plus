include_directories(.)

option(BUILD_GBA "Build GBA emulation" OFF)
option(BUILD_NDS "Build NDS emulation" OFF)
option(BUILD_SGB "Build SGB emulation" OFF)
option(BUILD_MIN "Build Pokemon Mini emulation" OFF)

add_subdirectory(common)
add_subdirectory(dmg)

if(BUILD_GBA)
    add_subdirectory(gba)
endif()

if(BUILD_SGB)
    add_subdirectory(sgb)
endif()

if(BUILD_NDS)
    add_subdirectory(nds)
endif()

if(BUILD_MIN)
    add_subdirectory(min)
endif()

if(QT_GUI)
    add_subdirectory(qt)
endif()

set(SRCS main.cpp)

SET(USER_HOME $ENV{HOME} CACHE STRING "Target User Home")
SET(USER $ENV{USER} CACHE STRING "Target User")

add_executable(gbe_plus ${SRCS})

if(BUILD_GBA)
    target_compile_definitions(gbe_plus PRIVATE GBE_BUILD_GBA)
    target_link_libraries(gbe_plus gba)
endif()

if(BUILD_SGB)
    target_compile_definitions(gbe_plus PRIVATE GBE_BUILD_SGB)
    target_link_libraries(gbe_plus sgb)
endif()

if(BUILD_NDS)
    target_compile_definitions(gbe_plus PRIVATE GBE_BUILD_NDS)
    target_link_libraries(gbe_plus nds)
endif()

if(BUILD_MIN)
    target_compile_definitions(gbe_plus PRIVATE GBE_BUILD_MIN)
    target_link_libraries(gbe_plus min)
endif()

target_link_libraries(gbe_plus common dmg)
target_link_libraries(gbe_plus ${SDL2_LIBRARY} ${SDL2MAIN_LIBRARY})

if (USE_SDL2_IMAGE)
    target_link_libraries(gbe_plus ${SDL2_IMAGE_LIBRARIES})
endif()

if (LINK_CABLE)
    target_link_libraries(gbe_plus ${SDL2NET_LIBRARY})
endif()

if (USE_OGL)
    target_link_libraries(gbe_plus ${OPENGL_gl_LIBRARY})
endif()

if (WIN32 AND USE_OGL)
    target_link_libraries(gbe_plus ${GLEW_LIBRARIES})
endif()

if(UNIX AND NOT APPLE)
	install(TARGETS gbe_plus DESTINATION /usr/local/bin)
	install(FILES gbe.ini DESTINATION ${USER_HOME}/.gbe_plus/)
	install(DIRECTORY data DESTINATION ${USER_HOME}/.gbe_plus)
	install(CODE "execute_process(COMMAND chown -R ${USER}:${USER} ${USER_HOME}/.gbe_plus)")
endif()
