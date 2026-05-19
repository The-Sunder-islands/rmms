cmake_minimum_required(VERSION 3.21)

project(rmms_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── FlatBuffers ──────────────────────────────────────────────────────────────
# flatc location: set -DFLATC_EXECUTABLE=/path/to/flatc or auto-find
if(NOT FLATC_EXECUTABLE)
    find_program(FLATC_EXECUTABLE flatc
        PATHS /tmp/flatbuffers
        DOC "FlatBuffers compiler (flatc)"
    )
endif()

if(NOT FLATC_EXECUTABLE)
    message(FATAL_ERROR "flatc not found. Set -DFLATC_EXECUTABLE=/path/to/flatc")
endif()

message(STATUS "Using flatc: ${FLATC_EXECUTABLE}")

set(FBS_SCHEMA "${CMAKE_SOURCE_DIR}/../proto/rmms.fbs")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/flatbuffers")

# Generate C++ headers from rmms.fbs
add_custom_command(
    OUTPUT "${GEN_DIR}/rmms_generated.h"
    COMMAND ${FLATC_EXECUTABLE} --cpp -o "${GEN_DIR}" "${FBS_SCHEMA}"
    DEPENDS "${FBS_SCHEMA}"
    COMMENT "Generating FlatBuffers C++ headers from rmms.fbs"
    VERBATIM
)

add_custom_target(flatbuffers_generated
    DEPENDS "${GEN_DIR}/rmms_generated.h"
)

# FlatBuffers runtime (header-only library shipped with flatc)
find_path(FLATBUFFERS_INCLUDE_DIR
    NAMES flatbuffers/flatbuffers.h
    PATHS /usr/include /usr/local/include
)

if(NOT FLATBUFFERS_INCLUDE_DIR)
    message(FATAL_ERROR "flatbuffers headers not found. Install flatbuffers-dev or flatbuffers package.")
endif()

message(STATUS "FlatBuffers include: ${FLATBUFFERS_INCLUDE_DIR}")

# ── Transport sources ────────────────────────────────────────────────────────
set(TRANSPORT_SOURCES
    src/transport/unix_socket.cpp
    src/transport/shared_mem.cpp
)

set(TRANSPORT_HEADERS
    src/transport/unix_socket.h
    src/transport/shared_mem.h
    src/transport/ring_buffer.h
)

# ── Protocol sources ─────────────────────────────────────────────────────────
set(PROTOCOL_SOURCES
    src/protocol/handler.cpp
    src/protocol/subscription.cpp
    src/protocol/server.cpp
)

set(PROTOCOL_HEADERS
    src/protocol/handler.h
    src/protocol/subscription.h
    src/protocol/server.h
)

# ── Backend library ──────────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/project_state.cpp
)

set(CORE_HEADERS
    src/core/project_state.h
)

set(HANDLER_SOURCES
    src/handlers/transport_handler.cpp
    src/handlers/track_handler.cpp
    src/handlers/clip_handler.cpp
    src/handlers/note_handler.cpp
    src/handlers/mixer_handler.cpp
    src/handlers/project_handler.cpp
    src/handlers/plugin_handler.cpp
    src/handlers/hybrid_handler.cpp
    src/handlers/chord_handler.cpp
    src/handlers/arranger_handler.cpp
    src/handlers/marker_handler.cpp
    src/handlers/tempo_handler.cpp
)

add_library(rmms_backend STATIC
    ${TRANSPORT_SOURCES}
    ${TRANSPORT_HEADERS}
    ${PROTOCOL_SOURCES}
    ${PROTOCOL_HEADERS}
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${HANDLER_SOURCES}
)

target_include_directories(rmms_backend PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${GEN_DIR}"
    "${FLATBUFFERS_INCLUDE_DIR}"
)

target_link_libraries(rmms_backend PUBLIC
    rt          # POSIX shm_open
    pthread     # std::thread
)

add_dependencies(rmms_backend flatbuffers_generated)

target_compile_options(rmms_backend PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Headless main executable (future: mock backend links here) ───────────────
# add_executable(rmms_backend_main src/main.cpp)
# target_link_libraries(rmms_backend_main PRIVATE rmms_backend)
