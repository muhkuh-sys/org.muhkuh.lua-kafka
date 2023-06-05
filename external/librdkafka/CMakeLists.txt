cmake_minimum_required(VERSION 3.7)

PROJECT("librdkafka")

INCLUDE(ExternalProject)

# Python is used for the patch tool.
# Python is used for some test scripts.
IF(CMAKE_VERSION VERSION_LESS 3.12.0)
	FIND_PACKAGE(PythonInterp 3 REQUIRED)
	SET(PYTHON_INTERPRETER "${PYTHON_EXECUTABLE}")
ELSE(CMAKE_VERSION VERSION_LESS 3.12.0)
	FIND_PACKAGE(Python3 REQUIRED COMPONENTS Interpreter)
	SET(PYTHON_INTERPRETER "${Python3_EXECUTABLE}")
ENDIF(CMAKE_VERSION VERSION_LESS 3.12.0)

#----------------------------------------------------------------------------
#
# Build the project.
#
SET(PRJ_CMAKE_ARGS "")

# Pass the toolchain file.
LIST(APPEND PRJ_CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")

# Build the static version.
LIST(APPEND PRJ_CMAKE_ARGS "-DRDKAFKA_BUILD_STATIC=ON")

# Do not build the examples.
LIST(APPEND PRJ_CMAKE_ARGS "-DRDKAFKA_BUILD_EXAMPLES=OFF")

# Do not build the tests.
LIST(APPEND PRJ_CMAKE_ARGS "-DRDKAFKA_BUILD_TESTS=OFF")

# Use LibreSSL.
LIST(APPEND PRJ_CMAKE_ARGS "-DWITH_SSL=ON")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_FOUND=1")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_INCLUDE_DIR=${CMAKE_BINARY_DIR}/libressl/install/include")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_CRYPTO_LIBRARY=${CMAKE_BINARY_DIR}/libressl/install/lib/libcrypto.a")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_SSL_LIBRARY=${CMAKE_BINARY_DIR}/libressl/install/lib/libssl.a")
LIST(APPEND PRJ_CMAKE_ARGS "-DOPENSSL_ROOT_DIR=${CMAKE_BINARY_DIR}/libressl/install")

# Set the CMake folder for zlib.
LIST(APPEND PRJ_CMAKE_ARGS "-DWITH_ZLIB=ON")
LIST(APPEND PRJ_CMAKE_ARGS "-Dnet.zlib-zlib_DIR=${net.zlib-zlib_DIR}")

# Do not build with SASL.
LIST(APPEND PRJ_CMAKE_ARGS "-DSASL_FOUND=0")
LIST(APPEND PRJ_CMAKE_ARGS "-DWITH_SASL=OFF")

ExternalProject_Add(TARGET_librdkafka
                    PREFIX ${CMAKE_CURRENT_BINARY_DIR}/librdkafka
                    URL ${CMAKE_CURRENT_SOURCE_DIR}/librdkafka-2.1.1.tar.gz
                    URL_HASH SHA1=fb4f717dc82c5a11304e1d689d011a04c2dcff21
                    PATCH_COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_HOME_DIRECTORY}/../cmake/tools/apply_patches.py --working-folder ${CMAKE_CURRENT_BINARY_DIR}/librdkafka/src/TARGET_librdkafka --patch-folder ${CMAKE_CURRENT_SOURCE_DIR}/patches --strip 1
                    CMAKE_ARGS ${PRJ_CMAKE_ARGS}
                    INSTALL_COMMAND make install DESTDIR=${CMAKE_CURRENT_BINARY_DIR}/install
)
