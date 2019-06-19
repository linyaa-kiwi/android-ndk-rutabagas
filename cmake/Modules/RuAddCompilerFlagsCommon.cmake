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

if(__ADD_COMPILER_FLAGS_COMMON)
    return()
endif()
set(__ADD_COMPILER_FLAGS_COMMON 1)

function(ru_get_compiler_flag_name result lang flag)
    set(flag_name "${flag}")
    string(REGEX REPLACE "^-" "" flag_name "${flag_name}")
    set(flag_name "${lang}_FLAG_${flag_name}")
    string(TOUPPER "${flag_name}" flag_name)
    string(REGEX REPLACE "[^0-9A-Z]" "_" flag_name "${flag_name}")
    set(${result} "${flag_name}" PARENT_SCOPE)
endfunction()

function(ru_get_compiler_flag_test_name result lang flag)
    ru_get_compiler_flag_name(flag_name "${lang}" "${flag}")
    set(${result} "HAVE_${flag_name}" PARENT_SCOPE)
endfunction()
