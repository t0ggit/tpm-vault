cmake_minimum_required(VERSION 3.16)
project(tpm-vault
    VERSION 1.0.0
    DESCRIPTION "Encrypted disk images with TPM2-sealed keys"
    LANGUAGES CXX
)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler warnings
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Werror=return-type
)

# Find required packages
find_package(PkgConfig REQUIRED)

# TPM2-TSS FAPI library
pkg_check_modules(TSS2_FAPI REQUIRED tss2-fapi)
pkg_check_modules(TSS2_RC REQUIRED tss2-rc)

# Source files
set(SOURCES
    src/main.cpp
    src/tpm_vault.cpp
    src/tpm_manager.cpp
    src/luks_manager.cpp
    src/loop_manager.cpp
    src/utils.cpp
)

# Executable
add_executable(tpm-vault ${SOURCES})

# Include directories
target_include_directories(tpm-vault PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${TSS2_FAPI_INCLUDE_DIRS}
    ${TSS2_RC_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(tpm-vault PRIVATE
    ${TSS2_FAPI_LIBRARIES}
    ${TSS2_RC_LIBRARIES}
)

# Compiler flags from pkg-config
target_compile_options(tpm-vault PRIVATE
    ${TSS2_FAPI_CFLAGS_OTHER}
    ${TSS2_RC_CFLAGS_OTHER}
)

# Installation
install(TARGETS tpm-vault
    RUNTIME DESTINATION bin
)

# Install man page if exists
if(EXISTS "${CMAKE_SOURCE_DIR}/man/tpm-vault.1")
    install(FILES man/tpm-vault.1
        DESTINATION share/man/man1
    )
endif()

# CPack configuration for packaging
set(CPACK_PACKAGE_NAME "tpm-vault")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_CONTACT "Your Name <your.email@example.com>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libtss2-fapi1, cryptsetup, util-linux, e2fsprogs")
include(CPack)

# Print configuration summary
message(STATUS "")
message(STATUS "tpm-vault ${PROJECT_VERSION}")
message(STATUS "")
message(STATUS "  C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "  TPM2-TSS FAPI: ${TSS2_FAPI_VERSION}")
message(STATUS "  TPM2-TSS RC:   ${TSS2_RC_VERSION}")
message(STATUS "")
