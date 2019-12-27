# Copyright 2019 Google Inc
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
#    list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# 3. Neither the name of Google Inc. nor the names of its contributors may be
#    used to endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

if(CMAKE_VERSION VERSION_LESS "3.7")
    message(WARNING
        [=[
        In CMake versions < 3.7, command `add_custom_command` does not support the DEPFILE argument.
        Therefore command `ru_add_spvnum` cannot track dependencies for GLSL `#include` directives.
        ]=]
    )
endif()

find_program(GLSLC glslc REQUIRED
    PATHS ${ANDROID_NDK}/shader-tools/${ANDROID_HOST_TAG}
    DOC "Path to glslc executable"
)

function(__ru_add_spirv_join_paths result a b)
    string(REGEX MATCH "^/" has_root "${b}")

    if(has_root)
        set(${result} "${b}" PARENT_SCOPE)
    else()
        set(${result} "${a}/${b}" PARENT_SCOPE)
    endif()
endfunction()

function(ru_add_spvnum out src)
    __ru_add_spirv_join_paths(abs_src "${CMAKE_CURRENT_SOURCE_DIR}" "${src}")
    file(RELATIVE_PATH rel_src "${CMAKE_BINARY_DIR}" "${abs_src}")

    __ru_add_spirv_join_paths(abs_out "${CMAKE_CURRENT_BINARY_DIR}" "${out}")
    file(RELATIVE_PATH rel_out "${CMAKE_BINARY_DIR}" "${abs_out}")

    if(CMAKE_VERSION VERSION_LESS "3.7")
        add_custom_command(
            DEPENDS ${src}
            OUTPUT ${out}
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMAND ${GLSLC} -MD -o "${rel_out}" -c -mfmt=num "${rel_src}"
        )
    else()
        add_custom_command(
            DEPENDS ${src}
            OUTPUT ${out}
            DEPFILE ${rel_out}.d
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMAND ${GLSLC} -MD -o "${rel_out}" -c -mfmt=num "${rel_src}"
        )
    endif()
endfunction()
