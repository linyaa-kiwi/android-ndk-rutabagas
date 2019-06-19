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

cmake_minimum_required(VERSION 3.6)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/Modules")

include(RuAddSpirV)
include(RuAddCFlag)
include(RuCheckCAttribute)

add_definitions("-D_POSIX_C_SOURCE=200809L")
add_definitions("-D_GNU_SOURCE")
add_definitions("-DLOG_TAG=\"ru-demo\"")

string(APPEND CMAKE_C_FLAGS " -std=c11")
string(APPEND CMAKE_C_FLAGS " -Wall")

ru_add_c_flag_checked(CMAKE_C_FLAGS "-Wignored-attributes")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Wunknown-attributes")

ru_add_c_flag_checked(CMAKE_C_FLAGS "-Wno-initializer-overrides")

ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=discarded-qualifiers")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=format")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=gnu-designator")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=implicit-function-declaration")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=implicit-int")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=incompatible-pointer-types")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=int-conversion")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=missing-parameter-type")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=missing-prototypes")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=return-type")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=shadow")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=sentinel")
ru_add_c_flag_checked(CMAKE_C_FLAGS "-Werror=writable-strings")

string(APPEND CMAKE_C_FLAGS_DEBUG " -g3")
string(APPEND CMAKE_C_FLAGS_DEBUG " -O0")
string(APPEND CMAKE_C_FLAGS_DEBUG " -DDEBUG")

# Warning: CMake 3.6.0-r2 requires that ';' be escaped in the below macro
# arguments; otherwise the macro splits that string on ';' into multiple args.
# CMake 3.7 does not require the escaping.
ru_check_c_attribute(alloc_size
    CODE
        [=[
        void *f(int n) __attribute__((alloc_size(1))) \\;
        void *g(int m, int n) __attribute__((alloc_size(1, 2))) \\;
        int main(void) { return 0 \\; }
        ]=]
)

configure_file(config.h.in config.h @ONLY)
string(APPEND CMAKE_C_FLAGS " -include \"${CMAKE_CURRENT_BINARY_DIR}/config.h\"")

add_subdirectory("src")
