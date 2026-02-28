cmake_minimum_required(VERSION 3.16)

project(ajazz
    VERSION 0.2.0
    DESCRIPTION "Ajazz AK35I V3 MAX keyboard control"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(ajazz
    src/main.cpp
    src/ajazz.cpp
    src/imgconv.cpp
    dependencies/stb/stb_impl.cpp
)

target_include_directories(ajazz PRIVATE src dependencies/stb dependencies)
target_compile_definitions(ajazz PRIVATE
    STBI_ONLY_JPEG STBI_ONLY_PNG STBI_ONLY_BMP STBI_ONLY_GIF)
target_compile_options(ajazz PRIVATE -Wall -Wextra -Wpedantic)

# ---------------------------------------------------------------------------
# Install rules (used by both "make install" and CPack)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS ajazz DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES udev/99-ajazz-ak35i.rules DESTINATION /etc/udev/rules.d)

# ---------------------------------------------------------------------------
# Packaging — CPack DEB
# ---------------------------------------------------------------------------

# postinst: reload udev rules after package install
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/postinst
"#!/bin/sh
set -e
udevadm control --reload-rules || true
udevadm trigger || true
")

# postrm: reload udev rules after package removal
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/postrm
"#!/bin/sh
set -e
udevadm control --reload-rules || true
")

set(CPACK_GENERATOR                       "DEB")
set(CPACK_PACKAGE_NAME                    "ajazz-ak35i")
set(CPACK_PACKAGE_VERSION                 ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY     ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_CONTACT                 "Daniel van Gerpen")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER       "Daniel van Gerpen")
set(CPACK_DEBIAN_PACKAGE_SECTION          "utils")
set(CPACK_PACKAGING_INSTALL_PREFIX        "/usr")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS        ON)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_BINARY_DIR}/postinst;${CMAKE_CURRENT_BINARY_DIR}/postrm")

include(CPack)
