##
## The widgets library ScriptHostWidgets, on top of the engine (core/):
## ScriptsWidget, ScriptEditorDialog, the approval dialog (useApprovalDialog)
## and the JavaScript highlighter. Not a file of the library template: it is
## added from the top-level CMakeLists.txt and builds the same profiles as
## core/ (ScriptHostWidgets_shared, _static and, with easy_profiler,
## _static_profile), each linking the engine of the same profile.
##

set(WIDGETS_LIBRARY_NAME ${LIBRARY_NAME}Widgets)
set(WIDGETS_MODULES Core Gui Widgets Qml)
find_package(${QT_PACKAGE_NAME} REQUIRED COMPONENTS ${WIDGETS_MODULES})
set(WIDGETS_QT_LIBS)
foreach(MODULE ${WIDGETS_MODULES})
    list(APPEND WIDGETS_QT_LIBS ${QT_PACKAGE_NAME}::${MODULE})
endforeach()

file(GLOB_RECURSE WIDGETS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/inc/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(WIDGETS_DEFINES SCRIPTHOSTWIDGETS_LIB QT_ENABLED QT_WIDGETS_ENABLED
    ${DEPENDENCY_NAME_MACRO} ${USER_SPECIFIC_DEFINES})

set(WIDGETS_PROFILES shared static)
if(EASY_PROFILER_IS_AVAILABLE)
    list(APPEND WIDGETS_PROFILES static_profile)
endif()

foreach(profile IN LISTS WIDGETS_PROFILES)
    set(target ${WIDGETS_LIBRARY_NAME}_${profile})
    if(profile STREQUAL "shared")
        add_library(${target} SHARED ${WIDGETS_SOURCES})
        target_compile_definitions(${target} PRIVATE ${WIDGETS_DEFINES} BUILD_SHARED)
        set(output ${WIDGETS_LIBRARY_NAME})
    elseif(profile STREQUAL "static")
        add_library(${target} STATIC ${WIDGETS_SOURCES})
        target_compile_definitions(${target} PRIVATE ${WIDGETS_DEFINES} BUILD_STATIC)
        set(output ${WIDGETS_LIBRARY_NAME}${STATIC_POSTFIX_STR})
    else()
        add_library(${target} STATIC ${WIDGETS_SOURCES})
        target_compile_definitions(${target} PRIVATE ${WIDGETS_DEFINES} BUILD_STATIC ${LIB_PROFILE_DEFINE})
        set(output ${WIDGETS_LIBRARY_NAME}${STATIC_POSTFIX_STR}${PROFILING_POSTFIX_STR})
    endif()

    set_target_properties(${target} PROPERTIES
        LINKER_LANGUAGE CXX
        OUTPUT_NAME     "${output}"
        DEBUG_POSTFIX   ${DEBUG_POSTFIX_STR}
        AUTOMOC         ON)

    target_link_libraries(${target} PUBLIC ${LIBRARY_NAME}_${profile} ${WIDGETS_QT_LIBS})

    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include/${LIBRARY_NAME}>
    )
endforeach()

# Installed next to the engine: the same lib folder, and the headers in the
# same include/ScriptHost folder.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    foreach(profile IN LISTS WIDGETS_PROFILES)
        install(TARGETS ${WIDGETS_LIBRARY_NAME}_${profile}
                RUNTIME DESTINATION "${INSTALL_LIB_PATH}"
                LIBRARY DESTINATION "${INSTALL_LIB_PATH}"
                ARCHIVE DESTINATION "${INSTALL_LIB_PATH}")
    endforeach()
    copyLibraryHeaders("${CMAKE_CURRENT_SOURCE_DIR}/inc" "${INSTALL_INCLUDE_PATH}" "${LIBRARY_NAME}")
endif()
