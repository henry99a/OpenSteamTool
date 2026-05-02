# Defines a static `nanopb` target by fetching the upstream nanopb/nanopb
# runtime sources via FetchContent (same pattern as Lua / Detours).
#
# The .pb.c / .pb.h files are pre-generated and checked into the repo under
# proto/ — end users do NOT need Python or protoc.


if(TARGET nanopb)
    return()
endif()

include(FetchCache)
include(FetchContent)

# ---- fetch nanopb ----
FetchContent_Declare(
    nanopb
    GIT_REPOSITORY https://github.com/nanopb/nanopb.git
    GIT_TAG        0.4.9.1
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    SOURCE_SUBDIR  cmake-noop
)
FetchContent_MakeAvailable(nanopb)

# ---- build nanopb runtime as a static library ----
add_library(nanopb STATIC
    ${nanopb_SOURCE_DIR}/pb_common.c
    ${nanopb_SOURCE_DIR}/pb_decode.c
    ${nanopb_SOURCE_DIR}/pb_encode.c
)
target_include_directories(nanopb PUBLIC "${nanopb_SOURCE_DIR}")
set_target_properties(nanopb PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(nanopb PRIVATE /w)
endif()
