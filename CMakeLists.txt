cmake_minimum_required(VERSION 3.16)
project(cpp-miner LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global build settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Target x86_64 Linux specifically; fail fast on anything else so we
# never silently produce a binary for the wrong ISA.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "This project targets Linux; other platforms are unsupported.")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    message(WARNING "This project targets x86_64; building for '${CMAKE_SYSTEM_PROCESSOR}' is unsupported.")
endif()

# Reasonable release optimization flags for a hashing-heavy workload.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -funroll-loops")

# ---------------------------------------------------------------------------
# Threading (pthreads, required for std::thread on Linux + affinity APIs)
# ---------------------------------------------------------------------------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Source files, grouped to mirror the /include and /src folder layout
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/config/ConfigManager.cpp
    src/utils/CpuInfo.cpp
    src/utils/MiniJson.cpp
    src/network/StratumClient.cpp
    src/miner/MinerEngine.cpp
    src/hash/Sha256dAlgorithm.cpp
    src/hash/HashAlgorithmFactory.cpp
    src/threading/ThreadPool.cpp
    src/logger/Logger.cpp
)

add_executable(cpp-miner ${SOURCES})

target_include_directories(cpp-miner PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(cpp-miner PRIVATE Threads::Threads)

target_compile_options(cpp-miner PRIVATE
    -Wall -Wextra -Wpedantic
)

# ---------------------------------------------------------------------------
# Installation (optional convenience: `cmake --install build`)
# ---------------------------------------------------------------------------
install(TARGETS cpp-miner RUNTIME DESTINATION bin)
