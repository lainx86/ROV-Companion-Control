cmake_minimum_required(VERSION 3.16)
project(rov_control_tui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Curses REQUIRED)
find_package(Threads REQUIRED)

add_library(rov_core
    src/config.cpp
    src/network.cpp
    src/process.cpp
    src/controller.cpp
)
target_include_directories(rov_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rov_core PUBLIC Threads::Threads)
target_compile_options(rov_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(rov_control src/main.cpp src/app.cpp src/app_view.cpp)
target_include_directories(rov_control PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(rov_control PRIVATE rov_core ${CURSES_LIBRARIES})
target_compile_options(rov_control PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE rov_core)
target_compile_options(core_tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME core_tests COMMAND core_tests)
set_tests_properties(core_tests PROPERTIES TIMEOUT 15)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME startup_scan_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_scan_tests.py
                $<TARGET_FILE:rov_control> ${CMAKE_CROSSCOMPILING_EMULATOR})
    set_tests_properties(startup_scan_tests PROPERTIES TIMEOUT 40)
endif()
