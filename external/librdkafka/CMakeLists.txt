cmake_minimum_required(VERSION 3.7)

PROJECT("librdkafka")

INCLUDE(ExternalProject)

# Python is used for some test scripts.
FIND_PACKAGE(PythonInterp 2.7 REQUIRED)

#----------------------------------------------------------------------------
#
# Build the project.
#
SET(PRJ_CMAKE_ARGS "")

# Pass the toolchain file.
LIST(APPEND PRJ_CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")

# Use LibreSSL.
LIST(APPEND PRJ_CMAKE_ARGS "-DWITH_SSL=ON")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_FOUND=1")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_INCLUDE_DIR=${CMAKE_BINARY_DIR}/libressl/install/include")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_CRYPTO_LIBRARY=${CMAKE_BINARY_DIR}/libressl/install/lib/libcrypto.a")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_SSL_LIBRARY=${CMAKE_BINARY_DIR}/libressl/install/lib/libssl.a")

# Set the CMake folder for zlib.
LIST(APPEND PRJ_CMAKE_ARGS "-DWITH_ZLIB=ON")
LIST(APPEND PRJ_CMAKE_ARGS "-Dnet.zlib-zlib_DIR=${net.zlib-zlib_DIR}")

# Do not build with SASL.
LIST(APPEND PRJ_CMAKE_ARGS "-DSASL_FOUND=0")
LIST(APPEND PRJ_CMAKE_ARGS "-DWITH_SASL=OFF")

ExternalProject_Add(TARGET_librdkafka
                    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/librdkafka
                    URL ${CMAKE_CURRENT_SOURCE_DIR}/librdkafka_src_git.tar.gz
                    URL_HASH SHA1=f943e25655a8b87f37bebb107aa38542f020c397
                    PATCH_COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_HOME_DIRECTORY}/../cmake/tools/apply_patches.py --working-folder ${CMAKE_CURRENT_BINARY_DIR}/librdkafka/src/TARGET_librdkafka --patch-folder ${CMAKE_CURRENT_SOURCE_DIR}/patches --strip 1
                    CMAKE_ARGS ${PRJ_CMAKE_ARGS}
                    INSTALL_COMMAND make install DESTDIR=${CMAKE_CURRENT_BINARY_DIR}/install
)
