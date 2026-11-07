# Game data extraction, included from overlay/CMakeLists.txt after the imgui target. It reads the
# game's pak and writes the three files the overlay reads (catalog.tsv, taxonomy.tsv, icons.rgba).
#   extract_core           static library with rd::extract::ExtractGameData (extract.h), which the
#                          overlay DLL runs in the game; zlib's inflate sources are compiled into it
#   byteboard-extract.exe  the same extraction from the command line: byteboard-extract <pak> <outDir>

# zlib is C.
enable_language(C)

set(EXTRACT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(ZLIB_DIR ${EXTRACT_DIR}/../third_party/zlib)

# zlib (git submodule at v1.3.1): only what inflate needs.
set(EXTRACT_ZLIB_SOURCES
    ${ZLIB_DIR}/adler32.c
    ${ZLIB_DIR}/crc32.c
    ${ZLIB_DIR}/inffast.c
    ${ZLIB_DIR}/inflate.c
    ${ZLIB_DIR}/inftrees.c
    ${ZLIB_DIR}/zutil.c)

add_library(extract_core STATIC
    ${EXTRACT_DIR}/catalog.cpp
    ${EXTRACT_DIR}/catalog.h
    ${EXTRACT_DIR}/extract.cpp
    ${EXTRACT_DIR}/extract.h
    ${EXTRACT_DIR}/icons.cpp
    ${EXTRACT_DIR}/icons.h
    ${EXTRACT_DIR}/log.h
    ${EXTRACT_DIR}/pak.cpp
    ${EXTRACT_DIR}/pak.h
    ${EXTRACT_DIR}/reader.cpp
    ${EXTRACT_DIR}/reader.h
    ${EXTRACT_DIR}/scan.cpp
    ${EXTRACT_DIR}/scan.h
    ${EXTRACT_DIR}/taxonomy.cpp
    ${EXTRACT_DIR}/taxonomy.h
    ${EXTRACT_DIR}/texts.cpp
    ${EXTRACT_DIR}/texts.h
    ${EXTRACT_DIR}/texture.cpp
    ${EXTRACT_DIR}/texture.h
    ${EXTRACT_DIR}/uasset.cpp
    ${EXTRACT_DIR}/uasset.h
    ${EXTRACT_DIR}/value.cpp
    ${EXTRACT_DIR}/value.h
    ${EXTRACT_ZLIB_SOURCES})
target_include_directories(extract_core PRIVATE ${ZLIB_DIR})
target_link_libraries(extract_core PUBLIC overlay_flags)

add_executable(byteboard-extract ${EXTRACT_DIR}/main.cpp)
target_link_libraries(byteboard-extract PRIVATE extract_core)
set_target_properties(byteboard-extract PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})
