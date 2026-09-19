cmake_minimum_required(VERSION 3.21)

project(rmms_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Project root (one level above backend/) ───────────────────────────────
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

# ── FlatBuffers headers (vendored in third_party/) ─────────────────────────
set(VENDORED_FLATBUF_INCLUDE "${PROJECT_ROOT}/third_party/flatbuffers/include")
set(FLATBUFFERS_INCLUDE_DIR "${VENDORED_FLATBUF_INCLUDE}" CACHE PATH
    "FlatBuffers header path" FORCE)
message(STATUS "FlatBuffers include: ${FLATBUFFERS_INCLUDE_DIR}")

# ── flatc: find or auto-download ──────────────────────────────────────────
if(NOT FLATC_EXECUTABLE)
    find_program(FLATC_EXECUTABLE flatc
        PATHS /tmp/flatbuffers
        DOC "FlatBuffers compiler (flatc)"
    )
endif()

if(NOT FLATC_EXECUTABLE)
    message(STATUS "flatc not found, downloading...")
    set(FLATC_VERSION "25.2.10")
    set(FLATC_DIR "${CMAKE_CURRENT_BINARY_DIR}/flatc-${FLATC_VERSION}")
    set(FLATC_ZIP "${CMAKE_CURRENT_BINARY_DIR}/flatc-${FLATC_VERSION}.zip")

    file(DOWNLOAD
        "https://github.com/google/flatbuffers/releases/download/v${FLATC_VERSION}/Linux.flatc.binary.clang++-18.zip"
        "${FLATC_ZIP}"
        STATUS FLATC_DL_STATUS
        SHOW_PROGRESS
    )

    list(GET FLATC_DL_STATUS 0 FLATC_DL_CODE)
    if(FLATC_DL_CODE EQUAL 0)
        file(ARCHIVE_EXTRACT INPUT "${FLATC_ZIP}" DESTINATION "${FLATC_DIR}")
        set(FLATC_EXECUTABLE "${FLATC_DIR}/flatc" CACHE FILEPATH "flatc binary" FORCE)
        file(REMOVE "${FLATC_ZIP}")
    endif()
endif()

if(NOT FLATC_EXECUTABLE OR NOT EXISTS "${FLATC_EXECUTABLE}")
    message(FATAL_ERROR
        "flatc not found and auto-download failed. "
        "Set -DFLATC_EXECUTABLE=/path/to/flatc manually."
    )
endif()

message(STATUS "Using flatc: ${FLATC_EXECUTABLE}")

# ── Codegen: rmms.fbs → C++ headers ───────────────────────────────────────
set(FBS_SCHEMA "${PROJECT_ROOT}/proto/rmms.fbs")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/flatbuffers")

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

# ── Codegen: rmms.fbs → Python bindings (for the e2e tests) ────────────────
set(GEN_PY_DIR "${CMAKE_CURRENT_BINARY_DIR}/py")

add_custom_command(
    OUTPUT "${GEN_PY_DIR}/rmms/Envelope.py"
    COMMAND ${FLATC_EXECUTABLE} --python -o "${GEN_PY_DIR}" "${FBS_SCHEMA}"
    DEPENDS "${FBS_SCHEMA}"
    COMMENT "Generating FlatBuffers Python bindings"
    VERBATIM
)

add_custom_target(flatbuffers_python
    DEPENDS "${GEN_PY_DIR}/rmms/Envelope.py"
)

# ── Source file lists ───────────────────────────────────────────────────────
set(TRANSPORT_SOURCES
    src/transport/unix_socket.cpp
    src/transport/shared_mem.cpp
)

set(TRANSPORT_HEADERS
    src/transport/unix_socket.h
    src/transport/shared_mem.h
    src/transport/ring_buffer.h
)

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

set(MOCK_SOURCES
    src/mock/mock_engine.cpp
)

set(MOCK_HEADERS
    src/mock/mock_engine.h
)

set(CORE_SOURCES
    src/core/in_memory_project_state.cpp
)

set(CORE_HEADERS
    src/core/iproject_state.h
    src/core/project_types.h
    src/core/in_memory_project_state.h
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
    src/handlers/subscription_handler.cpp
    src/handlers/ai_bridge_handler.cpp
)

set(AI_CLIENT_SOURCES
    src/ai_client/rest_client.cpp
    src/ai_client/sse_client.cpp
    src/ai_client/pipeline_translator.cpp
)

# ── Backend library ─────────────────────────────────────────────────────────
add_library(rmms_backend STATIC
    ${TRANSPORT_SOURCES}
    ${TRANSPORT_HEADERS}
    ${PROTOCOL_SOURCES}
    ${PROTOCOL_HEADERS}
    ${CORE_SOURCES}
    ${CORE_HEADERS}
    ${HANDLER_SOURCES}
    ${AI_CLIENT_SOURCES}
    ${MOCK_SOURCES}
    ${MOCK_HEADERS}
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

# ── Mock backend executable ─────────────────────────────────────────────────
add_executable(rmms_mock_backend main.cpp)
target_link_libraries(rmms_mock_backend PRIVATE rmms_backend)

# ── Python end-to-end tests (CTest) ─────────────────────────────────────────
# `ctest --test-dir build` runs test_mock.py + test_ai.py against a backend
# instance started by run_tests.py.
enable_testing()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import flatbuffers"
        RESULT_VARIABLE RMMS_FLATBUFFERS_PY_STATUS
        OUTPUT_QUIET ERROR_QUIET
    )
    if(RMMS_FLATBUFFERS_PY_STATUS EQUAL 0)
        add_dependencies(rmms_mock_backend flatbuffers_python)
        add_test(NAME rmms_backend_e2e
                 COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.py")
        set_tests_properties(rmms_backend_e2e PROPERTIES
            ENVIRONMENT "RMMS_PY_BINDINGS=${GEN_PY_DIR};RMMS_BACKEND_BIN=$<TARGET_FILE:rmms_mock_backend>"
            RUN_SERIAL TRUE
            TIMEOUT 240)
    else()
        message(STATUS "python 'flatbuffers' package missing - e2e tests disabled")
    endif()
else()
    message(STATUS "python3 not found - e2e tests disabled")
endif()
