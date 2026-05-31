set(CMAKE_AUTOMOC ON)

set(SRCS
	main.cpp
	main_menu.cpp
	render.cpp
	general_settings.cpp
	qt_common.cpp
	cgfx.cpp
	debug_dmg.cpp
	screens.cpp
	data_dialog.cpp
	cheat_menu.cpp
	rtc_menu.cpp
	pp2_menu.cpp
	zzh_menu.cpp
	ogl_manager.cpp
	con_ir_menu.cpp
	ps_menu.cpp
	mpos_menu.cpp
	tbf_menu.cpp
	utp_menu.cpp
	magic_reader_menu.cpp
	mw_menu.cpp
	)

set(HEADERS
	main_menu.h
	render.h
	general_settings.h
	qt_common.h
	cgfx.h
	debug_dmg.h
	screens.h
	data_dialog.h
	cheat_menu.h
	rtc_menu.h
	pp2_menu.h
	zzh_menu.h
	ogl_manager.h
	con_ir_menu.h
	ps_menu.h
	mpos_menu.h
	tbf_menu.h
	utp_menu.h
	magic_reader_menu.h
	mw_menu.h
	)

add_executable(gbe_plus_qt ${SRCS} ${HEADERS})
target_link_libraries(gbe_plus_qt common dmg ${GBE_QT_LIBS} ${SDL2_LIBRARY} ${SDL2MAIN_LIBRARY})

if (USE_SDL2_IMAGE)
    target_link_libraries(gbe_plus_qt ${SDL2_IMAGE_LIBRARIES})
endif()

if(BUILD_GBA)
    target_link_libraries(gbe_plus_qt gba)
endif()
if(BUILD_SGB)
    target_link_libraries(gbe_plus_qt sgb)
endif()
if(BUILD_NDS)
    target_link_libraries(gbe_plus_qt nds)
endif()
if(BUILD_MIN)
    target_link_libraries(gbe_plus_qt min)
endif()

if (USE_OGL)
    target_link_libraries(gbe_plus_qt ${OPENGL_gl_LIBRARY})
endif()

if (LINK_CABLE)
    target_link_libraries(gbe_plus_qt ${SDL2NET_LIBRARY})
endif()

if (WIN32 AND USE_OGL)
    target_link_libraries(gbe_plus_qt ${GLEW_LIBRARIES})
endif()

install(TARGETS gbe_plus_qt DESTINATION bin) 
