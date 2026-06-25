cmake_minimum_required(VERSION 3.16)

project(StackYanMacSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON REQUIRED libcjson)

add_executable(stackyan_sim
    src/SimApiServer.cpp
    src/SimHardware.cpp
    src/main.cpp
    ../main/events/EventBus.cpp
    ../main/tools/BuiltinTools.cpp
    ../main/tools/ToolRegistry.cpp
)

target_compile_definitions(stackyan_sim PRIVATE STACKYAN_SIMULATOR=1)

target_include_directories(stackyan_sim PRIVATE
    src
    ../main
    ../main/events
    ../main/hardware
    ../main/tools
    ../components/stackchu_hal/include
    ${CJSON_INCLUDE_DIRS}
)

target_link_directories(stackyan_sim PRIVATE ${CJSON_LIBRARY_DIRS})
target_link_libraries(stackyan_sim PRIVATE ${CJSON_LIBRARIES})

target_compile_options(stackyan_sim PRIVATE ${CJSON_CFLAGS_OTHER})
