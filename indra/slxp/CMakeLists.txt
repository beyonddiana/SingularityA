# -*- cmake -*-

project(slxp)

include(00-Common)
include(LLCommon)
include(LLAppearance)
include(LLCharacter)
include(LLMath)
include(LLPrimitive)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLAPPEARANCE_INCLUDE_DIRS}
    ${LLCHARACTER_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLPRIMITIVE_INCLUDE_DIRS}
    )

set(slxp_SOURCE_FILES
    slxp.cpp
    )

set(slxp_HEADER_FILES
    CMakeLists.txt
    slxp.h
    )

set_source_files_properties(${slxp_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND slxp_SOURCE_FILES ${slxp_HEADER_FILES})

add_library (slxp ${slxp_SOURCE_FILES})

target_link_libraries(
    slxp
    PUBLIC
    llcommon
    llappearance
    llcharacter
    llmath
    llprimitive
    )

