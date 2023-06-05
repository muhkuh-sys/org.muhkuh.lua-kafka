cmake_minimum_required(VERSION 3.7)

PROJECT("org.muhkuh.lua-kafka")

# Get the VCS version for the jonchki configuration.
INCLUDE(cmake/version.cmake)
INCLUDE(cmake/platform.cmake)
MESSAGE("Building for: ${JONCHKI_PLATFORM}")

# Enable tests (see here for details: http://www.vtk.org/Wiki/CMake/Testing_With_CTest)
ENABLE_TESTING()

# Python is used for the patch tool.
# Python is used for some test scripts.
IF(CMAKE_VERSION VERSION_LESS 3.12.0)
	FIND_PACKAGE(PythonInterp 3 REQUIRED)
	SET(PYTHON_INTERPRETER "${PYTHON_EXECUTABLE}")
ELSE(CMAKE_VERSION VERSION_LESS 3.12.0)
	FIND_PACKAGE(Python3 REQUIRED COMPONENTS Interpreter)
	SET(PYTHON_INTERPRETER "${Python3_EXECUTABLE}")
ENDIF(CMAKE_VERSION VERSION_LESS 3.12.0)


# Set some installation folders.
INCLUDE(cmake/muhkuh_installation_folders.cmake)

OPTION(BUILDCFG_ONLY_JONCHKI_CFG "Build only the jonchki configuration. This is used for the resolve phase. The default is OFF."
       "OFF")

#----------------------------------------------------------------------------
#
# Include the projects in the subdirectories.
#
ADD_SUBDIRECTORY(kafka)

#----------------------------------------------------------------------------
