# -*- cmake -*-

project(tko)

include(00-Common)
include(LLCommon)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    )

set(tko_SOURCE_FILES
    special_functionality.cpp
    )

set(tko_HEADER_FILES
    CMakeLists.txt
    special_functionality.h
    )

set_source_files_properties(${tko_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND tko_SOURCE_FILES ${tko_HEADER_FILES})

add_library (tko ${tko_SOURCE_FILES})

target_link_libraries(
    tko
    PUBLIC
    llcommon
    )

set_target_properties(tko PROPERTIES COMPILE_FLAGS "-DTKO_SPECIAL_FUNCTIONALITY={$TKO_SPECIAL_FUNCTIONALITY}")
