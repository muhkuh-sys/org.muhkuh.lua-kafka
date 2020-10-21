cmake_minimum_required(VERSION 2.8)

PROJECT("external")

INCLUDE(${PRJ_DIR}/cmake/platform.cmake)

SET(net.zlib-zlib_DIR "${CMAKE_BINARY_DIR}/../lua5.1/build_requirements/jonchki/install/dev/cmake")


#----------------------------------------------------------------------------
#
# Include the projects in the subdirectories.
#

# libressl
ADD_SUBDIRECTORY(libressl)

# librdkafka
ADD_SUBDIRECTORY(librdkafka)

#----------------------------------------------------------------------------
