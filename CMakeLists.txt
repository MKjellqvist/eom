cmake_minimum_required(VERSION 3.22)
project(eom)

find_package(PkgConfig)

pkg_check_modules(GTKMM gtkmm-3.0) # look into FindPkgConfig.cmake,
# it contains documentation
# Now the variables GTKMM_INCLUDE_DIRS, GTKMM_LIBRARY_DIRS and GTKMM_LIBRARIES
# contain what you expect

set(CMAKE_CXX_STANDARD 17)
link_directories(
        ${GTKMM_LIBRARY_DIRS})

include_directories(
        ${GTKMM_INCLUDE_DIRS})
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fsanitize=address -fno-omit-frame-pointer")
add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)
add_executable(eom main.cpp resources/resources.cpp)

target_link_libraries(eom
        ${GTKMM_LIBRARIES})