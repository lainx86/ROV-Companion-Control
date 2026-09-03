cmake_minimum_required(VERSION 3.16)
project(rov_control_tui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Curses REQUIRED)

add_executable(rov_control
    src/main.cpp
)
target_include_directories(rov_control PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(rov_control PRIVATE ${CURSES_LIBRARIES} pthread)
target_compile_options(rov_control PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_include_directories(core_tests PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(core_tests PRIVATE ${CURSES_LIBRARIES} pthread)
target_compile_options(core_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME core_tests COMMAND core_tests)
