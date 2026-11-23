cmake_minimum_required(VERSION 3.14)
project(minic VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MINIC_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

file(GLOB MINIC_CORE_SOURCES CONFIGURE_DEPENDS
     src/common/*.cpp src/frontend/*.cpp src/middle/*.cpp src/backend/*.cpp
     src/driver/pipeline.cpp src/driver/repl.cpp)

add_library(minic_core STATIC ${MINIC_CORE_SOURCES})
target_include_directories(minic_core PUBLIC src)

if(MSVC)
  target_compile_options(minic_core PUBLIC /W4)
else()
  target_compile_options(minic_core PUBLIC -Wall -Wextra -Wpedantic)
  if(MINIC_SANITIZE)
    target_compile_options(minic_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(minic_core PUBLIC -fsanitize=address,undefined)
  endif()
endif()

add_executable(minic src/driver/main.cpp)
target_link_libraries(minic PRIVATE minic_core)
# Put the binary in the source root so the test scripts find it as ./minic.
set_target_properties(minic PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_executable(unit_tests tests/unit/unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE minic_core)

enable_testing()
add_test(NAME unit COMMAND unit_tests)
add_test(NAME integration COMMAND ${CMAKE_SOURCE_DIR}/tests/run_tests.sh)
set_tests_properties(integration PROPERTIES ENVIRONMENT "MINIC=$<TARGET_FILE:minic>")

install(TARGETS minic RUNTIME DESTINATION bin)
