include_guard(DIRECTORY)
include(FetchContent)

set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}")

FetchContent_Declare(clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG 1.2.10
    GIT_SHALLOW TRUE)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE)
FetchContent_Declare(clap-wrapper
    GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
    GIT_TAG 2bc329c774b4584918c2150dda918afd5e6b2b87)
FetchContent_MakeAvailable(clap clap-wrapper)
if(CMAKE_SYSTEM_NAME STREQUAL "WASI" AND TARGET clap-wrapper-shared-detail)
    set_target_properties(clap-wrapper-shared-detail PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

function(core_add_to target)
    target_sources(${target} PRIVATE "${CORE_DIR}/messages.cpp" "${CORE_DIR}/resources.cpp")
    target_include_directories(${target} PUBLIC "${CORE_DIR}/..")
    target_link_libraries(${target} PUBLIC clap)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

function(core_ship_resources name folder)
    cmake_parse_arguments(ARG "" "" "DEPENDS" ${ARGN})
    foreach(format clap vst3 auv2 auv3 standalone aax)
        set(target ${name}_${format})
        if(NOT TARGET ${target})
            continue()
        endif()
        if(APPLE OR format MATCHES "vst3|aax")
            set(destination "$<TARGET_FILE_DIR:${target}>/../Resources")
        else()
            set(destination "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:${target}>.resources")
        endif()
        if(folder)
            add_custom_target(${target}_resources
                COMMAND ${CMAKE_COMMAND} -E rm -rf "${destination}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${folder}" "${destination}"
                VERBATIM)
            if(ARG_DEPENDS)
                add_dependencies(${target}_resources ${ARG_DEPENDS})
            endif()
            add_dependencies(${target} ${target}_resources)
        endif()
        # The linker signs only the binary; sign the bundle so its resources are
        # sealed too, or `codesign -v` (and stricter hosts) reject it. AUv3 and
        # AAX are signed by their own build steps.
        if(APPLE AND NOT format MATCHES "auv3|aax")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${target}>"
                VERBATIM)
        endif()
    endforeach()

    if(TARGET ${name}_wclap)
        target_link_options(${name}_wclap PRIVATE $<$<CONFIG:Release,MinSizeRel>:-Wl,--strip-debug>)
        set(bundle "$<TARGET_FILE_DIR:${name}_wclap>/${name}.wclap")
        set(contents module.wasm)
        set(copy "")
        if(folder)
            list(APPEND contents resources)
            set(copy COMMAND ${CMAKE_COMMAND} -E copy_directory "${folder}" "${bundle}/resources")
        endif()
        add_custom_target(${name}_wclap_bundle ALL
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${bundle}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${name}_wclap>" "${bundle}/module.wasm"
            ${copy}
            COMMAND ${CMAKE_COMMAND} -E chdir "${bundle}"
                    ${CMAKE_COMMAND} -E tar czf "../${name}.wclap.tar.gz" --format=gnutar ${contents}
            DEPENDS ${name}_wclap
            VERBATIM)
        if(ARG_DEPENDS)
            add_dependencies(${name}_wclap_bundle ${ARG_DEPENDS})
        endif()
    endif()
endfunction()
