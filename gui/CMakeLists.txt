find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network Concurrent)
add_library(sonshell_gui_core STATIC
  src/camera_session.cpp src/camera_session.h
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
