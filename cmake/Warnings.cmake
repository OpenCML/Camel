# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

# The warning set of this tree, written once.
#
# Three kinds of target compile C++ here -- the objects of the core library, the tools that link it
# (and the CPython extension, which is built by the same helper), and the modules that become .cmo --
# and each of the three files that create them spelled the flags out again: CMakeLists.txt for
# `libcamel_objects`, src/tools/libcamel_tool.cmake for every tool, cmake/camel_module.cmake for every
# module. A flag written in one of them stayed in one of them, and a new flag had to be written
# three times or it silently reached two thirds of the tree.
#
# The flags now appear here once each, and every consumer links the target that carries its list;
# no other file names a warning flag. The three consumers differ, and the difference is kept:
#
#   camel_warnings_libcamel   the core library's objects.
#   camel_warnings_tool       every tool, and the CPython extension built by the same helper.
#   camel_warnings_module     every module .cmo.
#
# What differs, and why:
#
#   -Wpedantic is on for the core library and the tools and off for the modules. That is not an
#   oversight in the module block: when the tool block got `-Wpedantic`, the module block was
#   written without it, and every flag added since -- `-Wno-missing-designated-field-initializers`,
#   `-Werror=dangling-gsl` -- was added to all three on purpose. Turning it on for the modules is a
#   change in what the build reports, not a move of a flag, so it stays off. Measured on this tree:
#   the groups it turns on report 0 diagnostics over the module objects today, so the difference
#   currently costs nothing either way -- which is why it is recorded here instead of being
#   resolved by guessing.
#
#   The four switches that turn extension diagnostics back off (`-Wno-c99-extensions`,
#   `-Wno-gnu-anonymous-struct`, `-Wno-nested-anon-types`, `-Wno-language-extension-token`) are
#   diagnostics that only `-Wpedantic` enables, so they travel with it.
#
#   -Wno-gnu-label-as-value is the core library's alone: the computed-goto interpreter,
#   src/stdlib/std/fvm/gotovm.cpp, is the one translation unit in the tree with `goto *`.
#
#   -Wno-nan-infinity-disabled is the core library's alone as well, and it is the one flag here
#   that nothing currently needs: it arrived with a `-ffast-math` this tree no longer passes
#   (removed from CMakeLists.txt, whose comment still says not to enable it), and it is kept where
#   it was rather than deleted or spread, because this change moves the set and does not change it.
#
# ORDER IS PART OF THE SET. `-Wno-<x>` wins over the group that enables `<x>` only by coming after
# it, so each consumer's list is assembled in the order the compiler is to read it, and nothing may
# append a flag of its own after these.

include_guard(GLOBAL)

# The two broad groups every target here compiles under.
set(CAMEL_WARNINGS_BROAD
    -Wall
    -Wextra
)

# -Wpedantic turns the extension diagnostics on; these sources use those extensions on purpose, so
# the group is written together with the switches that turn them off again.
set(CAMEL_WARNINGS_PEDANTIC
    -Wpedantic
    -Wno-c99-extensions # Uses flexible array members.
    -Wno-gnu-anonymous-struct
    -Wno-nested-anon-types
)

# The classes this tree turns off in every consumer, each because it is noise here and not because
# the diagnostic is wrong.
set(CAMEL_WARNINGS_QUIET
    -Wno-unused-parameter
    -Wno-unused-private-field
    # The largest class this build printed, and noise: 412 of the 547 warnings were this one, and
    # every one of them a designated initializer that names only the fields it differs from -- the
    # tagged unions that set `.kind` and one member (`Value{.kind = ValueKind::Int, .integer = v}`),
    # the specs that declare what they are about and take the rest from their defaults
    # (`ResourceSpec{.accesses = ..., .result = ...}`). Clang reports it with no flag of ours (on by
    # default since clang 19), and it is not the `-Wextra` diagnostic `-Wmissing-field-initializers`
    # (55, and staying on): that one is a braced list that names no field, where a field inserted in
    # the middle silently takes the next one's value. The field omitted here is value-initialized by
    # the language, which is what the writer meant; a real omission would show up as a default, so
    # the cost of this line is that adding a member to one of those structs reaches no initializer
    # that does not name it. The other choice is measured: with this class on, the 412 sit in front
    # of the 135 that are not this pattern, which is why nothing read either number.
    -Wno-missing-designated-field-initializers
)

# The one diagnostic this build fails on.
set(CAMEL_WARNINGS_FATAL
    # A view over a temporary is silent at run time until an element is read, and the first reader
    # is often in another function: `split.cpp`'s gate waits were a `std::span` over the vector
    # `gateWaitsOf` returns by value, and `.empty()` alone answered the right thing out of the
    # span's own size. Clang reports that as `-Wdangling-gsl` by default, so this build did print
    # it -- inside 546 other warnings that nothing fails on, which is why the defect stayed quiet.
    # This one diagnostic is what fails now. Not the `dangling` group: it also holds
    # `-Wreturn-stack-address`, whose one instance (`src/execute/limits.cpp:74`) returns a stack
    # address as a `uintptr_t` on purpose, and turning that red would only buy a pragma.
    -Werror=dangling-gsl
)

# The core library's own switches beyond the set above.
set(CAMEL_WARNINGS_LIBCAMEL_ONLY
    -Wno-gnu-label-as-value # for Computed Goto
    -Wno-nan-infinity-disabled
)

# The tools' own switch beyond the set above.
set(CAMEL_WARNINGS_TOOL_ONLY
    -Wno-language-extension-token
)

add_library(camel_warnings_libcamel INTERFACE)
target_compile_options(camel_warnings_libcamel INTERFACE
    ${CAMEL_WARNINGS_BROAD}
    ${CAMEL_WARNINGS_PEDANTIC}
    ${CAMEL_WARNINGS_LIBCAMEL_ONLY}
    ${CAMEL_WARNINGS_QUIET}
    ${CAMEL_WARNINGS_FATAL}
)

add_library(camel_warnings_tool INTERFACE)
target_compile_options(camel_warnings_tool INTERFACE
    ${CAMEL_WARNINGS_BROAD}
    ${CAMEL_WARNINGS_PEDANTIC}
    ${CAMEL_WARNINGS_TOOL_ONLY}
    ${CAMEL_WARNINGS_QUIET}
    ${CAMEL_WARNINGS_FATAL}
)

add_library(camel_warnings_module INTERFACE)
target_compile_options(camel_warnings_module INTERFACE
    ${CAMEL_WARNINGS_BROAD}
    ${CAMEL_WARNINGS_QUIET}
    ${CAMEL_WARNINGS_FATAL}
)