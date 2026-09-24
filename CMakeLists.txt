cmake_minimum_required(VERSION 3.16)

project(deepcool-cli VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6 Core
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

# Find udev and libusb
find_library(UDEV_LIBRARY udev)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

# ftxui (TUI library) via FetchContent
include(FetchContent)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG        v6.1.6
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(ftxui)

# Source files
set(SOURCES
    main_cli.cpp
    sensors.h
    sensors.cpp
    device.h
    device.cpp
    deepcooldevice.h
    deepcooldevice.cpp
)

add_executable(deepcool-cli ${SOURCES})

target_link_libraries(deepcool-cli
    Qt6::Core
    Qt6::Gui
    ${UDEV_LIBRARY}
    ${LIBUSB_LIBRARIES}
)
target_include_directories(deepcool-cli PRIVATE ${LIBUSB_INCLUDE_DIRS})

set_target_properties(deepcool-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS deepcool-cli RUNTIME DESTINATION bin)

# deepcool-tui: interactive TUI binary
add_executable(deepcool-tui
    main_tui.cpp
    device.h
    device.cpp
    deepcooldevice.h
    deepcooldevice.cpp
)
target_link_libraries(deepcool-tui
    ftxui::screen
    ftxui::dom
    ftxui::component
    Qt6::Core
    ${UDEV_LIBRARY}
    ${LIBUSB_LIBRARIES}
)
target_include_directories(deepcool-tui PRIVATE ${LIBUSB_INCLUDE_DIRS})
set_target_properties(deepcool-tui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
install(TARGETS deepcool-tui RUNTIME DESTINATION bin)

# Test tool — sends known fixed values to map packet slots to screen positions
add_executable(deepcool-test-mapping
    test_mapping.cpp
    device.h
    device.cpp
    deepcooldevice.h
    deepcooldevice.cpp
)
target_link_libraries(deepcool-test-mapping
    Qt6::Core
    ${UDEV_LIBRARY}
    ${LIBUSB_LIBRARIES}
)
target_include_directories(deepcool-test-mapping PRIVATE ${LIBUSB_INCLUDE_DIRS})
set_target_properties(deepcool-test-mapping PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
