cmake_minimum_required(VERSION 3.20)

project(BonjourServiceAdvertiser
    VERSION 1.0.0
    DESCRIPTION "Automatic Bonjour (mDNS/DNS-SD) advertisement of Windows services"
    LANGUAGES CXX
)

# ---------------------------------------------------------------------------
# Require C++20
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Windows-only
# ---------------------------------------------------------------------------
if (NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only.")
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SOURCES
    src/main.cpp
    src/logger.cpp
    src/config.cpp
    src/port_checker.cpp
    src/bonjour_publisher.cpp
    src/detector_manager.cpp
    src/service_worker.cpp
    src/detectors/all_detectors.cpp
)

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(BonjourServiceAdvertiser WIN32 ${SOURCES})

# No console window; entry point is wmain
set_target_properties(BonjourServiceAdvertiser PROPERTIES
    WIN32_EXECUTABLE FALSE   # We want a console-subsystem exe so SCM can run it
    LINK_FLAGS "-municode"   # Use wide wmain (MinGW)
)

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
target_include_directories(BonjourServiceAdvertiser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(BonjourServiceAdvertiser PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    NOMINMAX
    _WIN32_WINNT=0x0A00   # Windows 10
)

# ---------------------------------------------------------------------------
# Compiler flags (MinGW-w64)
# ---------------------------------------------------------------------------
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(BonjourServiceAdvertiser PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -O2
    )
endif()

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_libraries(BonjourServiceAdvertiser PRIVATE
    ws2_32
    iphlpapi
    advapi32
    winspool     # EnumPrintersW
    shell32      # SHGetFolderPathW
    ole32
)

# ---------------------------------------------------------------------------
# Static runtime (no external MinGW DLL dependency)
# ---------------------------------------------------------------------------
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_options(BonjourServiceAdvertiser PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

# ---------------------------------------------------------------------------
# Version resource (embed version info into the .exe)
# ---------------------------------------------------------------------------
if (WIN32 AND MINGW)
    set(VERSION_RC "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
    configure_file(
        "${CMAKE_SOURCE_DIR}/installer/version.rc.in"
        "${VERSION_RC}"
        @ONLY
    )
    target_sources(BonjourServiceAdvertiser PRIVATE "${VERSION_RC}")
endif()

# ---------------------------------------------------------------------------
# Install rules (used by the WiX installer harvest step)
# ---------------------------------------------------------------------------
install(TARGETS BonjourServiceAdvertiser
    RUNTIME DESTINATION bin
)
install(FILES
    "${CMAKE_SOURCE_DIR}/installer/advertiser.ini.default"
    DESTINATION config
    RENAME advertiser.ini
)
