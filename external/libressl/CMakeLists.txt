cmake_minimum_required(VERSION 3.7)

PROJECT("libressl")

INCLUDE(ExternalProject)

# Python is used for some test scripts.
FIND_PACKAGE(PythonInterp 2.7 REQUIRED)


#----------------------------------------------------------------------------
#
# Build the project.
#
SET(PRJ_CMAKE_ARGS "")

# Do not build the apps.
LIST(APPEND PRJ_CMAKE_ARGS "-DLIBRESSL_APPS=OFF")

# Do not build the tests.
LIST(APPEND PRJ_CMAKE_ARGS "-DLIBRESSL_TESTS=OFF")

# Do not allow ASM code.
LIST(APPEND PRJ_CMAKE_ARGS "-DENABLE_ASM=OFF")

LIST(APPEND PRJ_CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
LIST(APPEND PRJ_CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX=''")

ExternalProject_Add(TARGET_libressl
                    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/libressl
                    URL ${CMAKE_CURRENT_SOURCE_DIR}/libressl-3.1.4.tar.gz
                    URL_HASH SHA1=084a9c99eaaa37427a50247dc5862087f1804f88
                    PATCH_COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_HOME_DIRECTORY}/../cmake/tools/apply_patches.py --working-folder ${CMAKE_CURRENT_BINARY_DIR}/libressl/src/TARGET_libressl --patch-folder ${CMAKE_CURRENT_SOURCE_DIR}/patches --strip 1
                    CMAKE_ARGS ${PRJ_CMAKE_ARGS}
                    INSTALL_COMMAND make install DESTDIR=${CMAKE_CURRENT_BINARY_DIR}/install
)
