find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network Concurrent)
add_library(sonshell_gui_core STATIC
  src/about_dialog.cpp src/about_dialog.h
  src/video_preview.cpp src/video_preview.h
  src/language_support.cpp src/language_support.h
  src/help_dialog.cpp src/help_dialog.h
  src/camera_controls_widget.cpp src/camera_controls_widget.h
  src/exposure_control.cpp src/exposure_control.h
  src/camera_session.cpp src/camera_session.h
  src/camera_card.cpp src/camera_card.h
  src/import_verification.cpp src/import_verification.h
  src/camera_card_widget.cpp src/camera_card_widget.h
  src/settings_store.cpp src/settings_store.h
  src/image_viewer.cpp src/image_viewer.h
  src/photo_library.cpp src/photo_library.h
  src/media_reader.cpp src/media_reader.h
  src/studio_review.cpp src/studio_review.h
  src/gallery_widget.cpp src/gallery_widget.h
  src/main_window.cpp src/main_window.h)
target_include_directories(sonshell_gui_core PUBLIC src)
target_link_libraries(sonshell_gui_core PUBLIC Qt6::Widgets Qt6::Network Qt6::Concurrent)
target_compile_options(sonshell_gui_core PRIVATE -Wall -Wextra -Wpedantic)
add_executable(sonshell-remote main.cpp)
target_link_libraries(sonshell-remote PRIVATE sonshell_gui_core)
target_compile_definitions(sonshell-remote PRIVATE SONSHELL_REMOTE_VERSION="${PROJECT_VERSION}")
install(TARGETS sonshell-remote RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# lrelease is a build dependency only; the installed app embeds its catalog/help.
find_program(QT_LRELEASE_EXECUTABLE NAMES lrelease-qt6 lrelease
  HINTS /usr/lib/qt6/bin REQUIRED)
set(RU_QM "${CMAKE_CURRENT_BINARY_DIR}/sonshell_RU.qm")
add_custom_command(OUTPUT "${RU_QM}"
  COMMAND "${QT_LRELEASE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/i18n/sonshell_RU.ts" -qm "${RU_QM}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/sonshell_RU.ts" VERBATIM)
configure_file(remote_resources.qrc.in "${CMAKE_CURRENT_BINARY_DIR}/remote_resources.qrc" @ONLY)
qt_add_resources(REMOTE_RESOURCES "${CMAKE_CURRENT_BINARY_DIR}/remote_resources.qrc")
target_sources(sonshell_gui_core PRIVATE ${REMOTE_RESOURCES} "${RU_QM}")
