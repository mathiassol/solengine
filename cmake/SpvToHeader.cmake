# SpvToHeader.cmake
# Called by CMake -P to convert a SPIR-V binary file into a C header (uint32_t[]).
# Variables:  SPV_FILE  — input .spv path
#             HDR_FILE  — output .h path
#             VARNAME   — C variable name (e.g., pbr_vert_glsl)

file(READ "${SPV_FILE}" SPV_DATA HEX)

# Split hex string into pairs, then groups of 8 hex digits (4 bytes = 1 uint32_t)
string(LENGTH "${SPV_DATA}" HEX_LEN)
math(EXPR WORD_COUNT "${HEX_LEN} / 8")

set(WORDS "")
foreach(I RANGE 0 ${WORD_COUNT})
    math(EXPR OFFSET "${I} * 8")
    if(OFFSET LESS HEX_LEN)
        string(SUBSTRING "${SPV_DATA}" ${OFFSET} 8 CHUNK)
        # Reverse byte order (little-endian on all our platforms)
        string(SUBSTRING "${CHUNK}" 0 2 B0)
        string(SUBSTRING "${CHUNK}" 2 2 B1)
        string(SUBSTRING "${CHUNK}" 4 2 B2)
        string(SUBSTRING "${CHUNK}" 6 2 B3)
        list(APPEND WORDS "0x${B3}${B2}${B1}${B0}u")
    endif()
endforeach()

list(JOIN WORDS ", " WORDS_STR)

file(WRITE "${HDR_FILE}"
"// Auto-generated — do not edit. Source: ${SPV_FILE}
#pragma once
#include <cstdint>
static const uint32_t ${VARNAME}[] = { ${WORDS_STR} };
static const uint32_t ${VARNAME}_size = sizeof(${VARNAME});
")
