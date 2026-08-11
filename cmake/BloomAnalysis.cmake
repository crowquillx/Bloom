# Opt-in compiler instrumentation used by local and scheduled deep checks.
# Normal developer, pull-request, packaging, and release builds remain unchanged.
set(BLOOM_SANITIZER "none" CACHE STRING "Compiler sanitizer: none, address, or thread")
set_property(CACHE BLOOM_SANITIZER PROPERTY STRINGS none address thread)
option(BLOOM_ENABLE_COVERAGE "Instrument Bloom tests for gcov-compatible coverage" OFF)

set(_bloom_valid_sanitizers none address thread)
if(NOT BLOOM_SANITIZER IN_LIST _bloom_valid_sanitizers)
    message(FATAL_ERROR
        "Invalid BLOOM_SANITIZER '${BLOOM_SANITIZER}'. Expected one of: ${_bloom_valid_sanitizers}")
endif()

if((NOT BLOOM_SANITIZER STREQUAL "none" OR BLOOM_ENABLE_COVERAGE)
   AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "Bloom analysis instrumentation requires GCC or Clang")
endif()

if(BLOOM_ENABLE_COVERAGE AND NOT BLOOM_SANITIZER STREQUAL "none")
    message(FATAL_ERROR "Coverage and sanitizer instrumentation use separate build trees")
endif()

if(BLOOM_SANITIZER STREQUAL "address")
    # Several focused tests intentionally link narrow PlayerController stubs;
    # vptr instrumentation requires the omitted facade RTTI. All other UBSan
    # checks remain enabled alongside ASan.
    add_compile_options(
        -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
    add_link_options(
        -fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
elseif(BLOOM_SANITIZER STREQUAL "thread")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread -fno-omit-frame-pointer)
elseif(BLOOM_ENABLE_COVERAGE)
    add_compile_options(--coverage -fprofile-abs-path -fno-omit-frame-pointer)
    add_link_options(--coverage)
endif()
