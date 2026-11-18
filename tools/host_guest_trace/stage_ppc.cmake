# Private build inputs at stable paths let Ninja reuse unchanged chunks when
# the validated generated corpus moves. COPYONLY preserves timestamps for
# identical content. This is source staging, not a trusted-object cache.
function(stage_private_ppc input_directory staging_directory expected_count)
    if(NOT IS_DIRECTORY "${input_directory}" OR IS_SYMLINK "${input_directory}"
            OR IS_SYMLINK "${staging_directory}"
            OR (EXISTS "${staging_directory}" AND NOT IS_DIRECTORY "${staging_directory}"))
        message(FATAL_ERROR "Invalid private source staging directory")
    endif()
    if(NOT "${expected_count}" MATCHES "^[1-9][0-9]*$" OR expected_count GREATER 4096)
        message(FATAL_ERROR "Private source staging count is outside bounds")
    endif()
    file(GLOB chunks "${input_directory}/ppc_recomp.*.cpp")
    list(SORT chunks COMPARE NATURAL)
    list(LENGTH chunks count)
    if(NOT count EQUAL expected_count)
        message(FATAL_ERROR "Private source staging count mismatch")
    endif()
    set(inputs ${chunks}
        "${input_directory}/ppc_context.h" "${input_directory}/ppc_config.h"
        "${input_directory}/ppc_recomp_shared.h" "${input_directory}/ppc_func_mapping.cpp")
    # Validate the entire set before changing any output; no links or directories
    # are accepted as source/destination files, including dangling symlinks.
    foreach(source IN LISTS inputs)
        get_filename_component(name "${source}" NAME)
        if(NOT name MATCHES "^(ppc_recomp\.[0-9]+\.cpp|ppc_context\.h|ppc_config\.h|ppc_recomp_shared\.h|ppc_func_mapping\.cpp)$"
                OR NOT EXISTS "${source}" OR IS_SYMLINK "${source}" OR IS_DIRECTORY "${source}"
                OR IS_SYMLINK "${staging_directory}/${name}" OR IS_DIRECTORY "${staging_directory}/${name}")
            message(FATAL_ERROR "Invalid private source staging file")
        endif()
    endforeach()
    file(MAKE_DIRECTORY "${staging_directory}")
    set(staged_chunks)
    foreach(source IN LISTS inputs)
        get_filename_component(name "${source}" NAME)
        # Reconfiguring a native-only dependency may explicitly use the already
        # validated staging directory. Do not read/compare a large file against
        # itself (or request needless cloud-backed filesystem hydration).
        if(NOT "${source}" STREQUAL "${staging_directory}/${name}")
            configure_file("${source}" "${staging_directory}/${name}" COPYONLY)
        endif()
        if(source IN_LIST chunks)
            list(APPEND staged_chunks "${staging_directory}/${name}")
        endif()
    endforeach()
    # Never glob the destination: old owned files may remain but are not inputs
    # to this target. No recursive cleanup and no mutation of the original corpus.
    set(STAGED_PPC_CHUNKS "${staged_chunks}" PARENT_SCOPE)
endfunction()
