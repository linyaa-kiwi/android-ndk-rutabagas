#!/usr/bin/env python3

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

import argparse
import sys
from textwrap import dedent

template = dedent("""\
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

    include(Check{lang:u}SourceCompiles)
    include(RuAdd{lang:u}Flag)

    function(ru_get_{lang:l}_attribute_test_name result attrib)
        string(TOUPPER "HAVE_{lang:u}_ATTRIBUTE_${{attrib}}" name)
        set("${{result}}" "${{name}}" PARENT_SCOPE)
    endfunction()

    # Check if the {lang:h} compiler supports a given attribute.
    #
    #   ru_check_{lang:l}_attribute(<attrib> CODE <code>)
    #
    # Test if the {lang:h} compiler supports <attrib>, and store
    # the test result in internal cache variable HAVE_{lang:u}_ATTRIBUTE_${{attrib_name}},
    # where attrib_name is the result of converting "${{attrib}}"
    # to uppercase and sanitizing it into a legal CMake variable name.
    #
    # The test respects the same global variables as check_{lang:l}_source_compiles.
    #
    # Before checking if the compiler supports the attribute, the function
    # first checks if the compiler supports the flags
    # "-Werror=ignored-attribute" and "-Werror=unknown-attributes" with
    # ru_check_{lang:l}_flag, and adds each supported flag to
    # CMAKE_REQUIRED_FLAGS at function-scope.
    #
    # Example:
    #
    #     If the compiler supports the "alloc_size" attribute, then
    #
    #         ru_check_{lang:l}_attribute(alloc_size
    #             CODE
    #                 [=[
    #                 void *f(int n) __attribute__((alloc_size(1)));
    #                 void *g(int m, int n) __attribute__((alloc_size(1, 2)));
    #                 int main(void) {{ return 0; }}
    #                 ]=]
    #         )
    #
    #     will set the cache variable
    #
    #         HAVE_{lang:u}_ATTRIBUTE_ALLOC_SIZE:INTERNAL=1
    #
    #     Otherwise, it will set
    #
    #         HAVE_{lang:u}_ATTRIBUTE_ALLOC_SIZE:INTERNAL=
    #
    function(ru_check_{lang:l}_attribute attrib)
        cmake_parse_arguments(
            PARSE_ARGV 1
            "ARG" # prefix
            "" # options
            "CODE" # one-value-keywords
            "" # multi-value-keywords
        )

        if(NOT ARG_CODE)
            message(FATAL_ERROR "in ru_check_{lang:l}_attribute: CODE keyword argument is missing")
        endif()

        if(ARG_UNPARSED_ARGUMENTS)
            message(FATAL_ERROR "in ru_check_{lang:l}_attribute: found unknown arguments: ${{ARG_UNPARSED_ARGUMENTS}}")
        endif()

        ru_check_{lang:l}_flag("-Werror=ignored-attributes")
        if(HAVE_{lang:u}_FLAG_WERROR_IGNORED_ATTRIBUTES)
            string(APPEND CMAKE_REQUIRED_FLAGS " -Werror=ignored-attributes")
        endif()

        ru_check_{lang:l}_flag("-Werror=unknown-attributes")
        if(HAVE_{lang:u}_FLAG_WERROR_UNKNOWN_ATTRIBUTES)
            string(APPEND CMAKE_REQUIRED_FLAGS " -Werror=unknown-attributes")
        endif()

        ru_get_{lang:l}_attribute_test_name(test_name "${{attrib}}")

        # Replace the messages from check_{lang:l}_source_compiles with custom,
        # more helpful messages. But mimic the style of the original messages.
        set(CMAKE_REQUIRED_QUIET ON)
        set(msg "Performing Test ${{test_name}} for {lang:h} attribute \\"${{attrib}}\\"")

        message(STATUS "${{msg}}")
        check_{lang:l}_source_compiles("${{ARG_CODE}}" ${{test_name}})

        if(${{test_name}})
            message(STATUS "${{msg}} - Success")
        else()
            message(STATUS "${{msg}} - Failed")
        endif()
    endfunction()
    """)

class LangName:

    __slots__ = ('human', 'cmake')

    def __init__(self, *, human, cmake):
        self.human = human
        self.cmake = cmake

    def __format__(self, format_spec):
        if format_spec == 'h':
            return self.human
        elif format_spec == 'l':
            return self.cmake.lower()
        elif format_spec == 'u':
            return self.cmake.upper()
        else:
            assert False

def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument('--lang', choices=('c', 'c++'))
    return ap.parse_args()

def main():
    pargs = parse_args()

    if pargs.lang == 'c':
        lang = LangName(human='C', cmake='C')
    elif pargs.lang == 'c++':
        lang = LangName(human='C++', cmake='CXX')
    else:
        assert False

    sys.stdout.write(template.format(lang=lang))

if __name__== '__main__':
    main()
