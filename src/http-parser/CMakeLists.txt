cmake_minimum_required (VERSION 2.8)

file(GLOB SOURCES "./" "*.c")

include_directories("${PROJECT_SOURCE_DIR}")

add_library(http-parser STATIC
    ${SOURCES}
)

install (TARGETS http-parser DESTINATION lib)
