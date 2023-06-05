cmake_minimum_required(VERSION 2.8)

PROJECT("external")

INCLUDE(${PRJ_DIR}/cmake/platform.cmake)

SET(net.zlib-zlib_DIR "${CMAKE_BINARY_DIR}/../lua5.4/build_requirements/jonchki/install/dev/cmake")

# Python is used for the patch tool.
IF(CMAKE_VERSION VERSION_LESS 3.12.0)
	FIND_PACKAGE(PythonInterp 3 REQUIRED)
	SET(PYTHON_INTERPRETER "${PYTHON_EXECUTABLE}")
ELSE(CMAKE_VERSION VERSION_LESS 3.12.0)
	FIND_PACKAGE(Python3 REQUIRED COMPONENTS Interpreter)
	SET(PYTHON_INTERPRETER "${Python3_EXECUTABLE}")
ENDIF(CMAKE_VERSION VERSION_LESS 3.12.0)

#----------------------------------------------------------------------------
#
# Include the projects in the subdirectories.
#

# libressl
ADD_SUBDIRECTORY(libressl)

# librdkafka
ADD_SUBDIRECTORY(librdkafka)

#----------------------------------------------------------------------------
