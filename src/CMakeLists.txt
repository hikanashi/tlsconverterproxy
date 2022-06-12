cmake_minimum_required (VERSION 2.8)

add_subdirectory ("http-parser")

file(GLOB SOURCES "./" "*.cpp")

include_directories("${PROJECT_SOURCE_DIR}")


add_library(dummyproxy STATIC
    ${SOURCES}
)

target_link_libraries (dummyproxy http-parser)

install (TARGETS dummyproxy DESTINATION lib)
