cmake_minimum_required(VERSION 3.2.3)

PROJECT("kafka")

INCLUDE(ExternalProject)

SET(VERSION_EXTERNAL "0.0.3")
SET(VERSION_PACKAGE  "2")
SET(PROJECT_VERSION  "${VERSION_EXTERNAL}.${VERSION_PACKAGE}")

# Get the VCS version for the jonchki configuration.
INCLUDE(${CMAKE_HOME_DIRECTORY}/cmake/version.cmake)

# Filter the jonchki configuration.
CONFIGURE_FILE(installer/lua${BUILDCFG_LUA_VERSION}/kafka.xml
               ${CMAKE_BINARY_DIR}/lua${BUILDCFG_LUA_VERSION}-kafka-${PROJECT_VERSION}.xml
               @ONLY)

OPTION(BUILDCFG_ONLY_JONCHKI_CFG "Build only the jonchki configuration. This is used for the resolve phase. The default is OFF."
       "OFF")

#----------------------------------------------------------------------------
#
# Build the project.
#

IF((${BUILDCFG_ONLY_JONCHKI_CFG} STREQUAL "OFF"))
	# Select the LUA version.
	INCLUDE(${CMAKE_HOME_DIRECTORY}/cmake/muhkuh_lua.cmake)

	ENABLE_TESTING()
	FIND_PACKAGE(PythonInterp 2.7 REQUIRED)

	FIND_PACKAGE(SWIG REQUIRED)

	FIND_PACKAGE(net.zlib-zlib REQUIRED CONFIG)
	GET_TARGET_PROPERTY(ZLIB_LIBRARY net.zlib-zlib::zlibstatic LOCATION)
	SET(ZLIB_INCLUDE_DIR ${net.zlib-zlib_INCLUDE_DIR})
	MESSAGE("ZLIB_LIBRARY: ${ZLIB_LIBRARY}")
	MESSAGE("ZLIB_INCLUDE_DIR: ${ZLIB_INCLUDE_DIR}")

	# Add a custom target for the swig runtime for LUA.
	INCLUDE(${CMAKE_HOME_DIRECTORY}/cmake/muhkuh_swig_lua_runtime.cmake)

	INCLUDE(${SWIG_USE_FILE})

	SET(OPENSSL_ROOT_DIR "${CMAKE_BINARY_DIR}/../external/libressl/install")
	FIND_PACKAGE(OpenSSL REQUIRED)

	SET(RdKafka_DIR "${CMAKE_BINARY_DIR}/../external/librdkafka/install/usr/local/lib/cmake/RdKafka")
	FIND_PACKAGE(RdKafka REQUIRED)
	GET_TARGET_PROPERTY(RDKAFKA_LIBRARY RdKafka::rdkafka LOCATION)
	MESSAGE("RDKAFKA_LIBRARY = ${RDKAFKA_LIBRARY}")

	SET_SOURCE_FILES_PROPERTIES(kafka.i PROPERTIES CPLUSPLUS ON)
	SET_SOURCE_FILES_PROPERTIES(kafka.i PROPERTIES SWIG_FLAGS "")
	SWIG_ADD_MODULE(TARGET_kafka lua kafka.i wrapper.cpp)
	SWIG_LINK_LIBRARIES(TARGET_kafka RdKafka::rdkafka)
	IF((${CMAKE_SYSTEM_NAME} STREQUAL "Windows") AND (${CMAKE_COMPILER_IS_GNUCC}))
		SWIG_LINK_LIBRARIES(TARGET_kafka ${LUA_LIBRARIES})
	ENDIF((${CMAKE_SYSTEM_NAME} STREQUAL "Windows") AND (${CMAKE_COMPILER_IS_GNUCC}))
	TARGET_INCLUDE_DIRECTORIES(TARGET_kafka
	                           PRIVATE ${LUA_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/swig_runtime ${CMAKE_CURRENT_SOURCE_DIR})
	TARGET_COMPILE_DEFINITIONS(TARGET_kafka
	                           PRIVATE DIST_VERSION="${PROJECT_VERSION}")
	ADD_DEPENDENCIES(TARGET_kafka TARGET_swigluarun)

	# Set the name of the output file to "kafka".
	SET_TARGET_PROPERTIES(TARGET_kafka PROPERTIES PREFIX "" OUTPUT_NAME "kafka")

	# On mingw link all compiler libraries static.
	IF((${CMAKE_SYSTEM_NAME} STREQUAL "Windows") AND (${CMAKE_COMPILER_IS_GNUCC}))
		SET_PROPERTY(TARGET TARGET_kafka PROPERTY LINK_FLAGS "-static -static-libgcc -static-libstdc++")
	ENDIF((${CMAKE_SYSTEM_NAME} STREQUAL "Windows") AND (${CMAKE_COMPILER_IS_GNUCC}))

	# Install the lua module.
	INSTALL(TARGETS TARGET_kafka
	        EXPORT EXPORT_package
	        LIBRARY DESTINATION ${INSTALL_DIR_LUA_MODULES}
	        ARCHIVE DESTINATION lib
	        RUNTIME DESTINATION bin
	        INCLUDES DESTINATION include)

	# Add tests for this module.
	IF((${CMAKE_SYSTEM_NAME} STREQUAL "Windows") AND (${CMAKE_COMPILER_IS_GNUCC}))
		# Here are the MinGW specific tests.
		ADD_TEST(NAME kafka_MinGW_DLL_dependencies
		         COMMAND "${PYTHON_EXECUTABLE}" ${TEST_SCRIPT_FOLDER}/mingw_dll_dependencies.py -u lua5.1 -u lua5.2 -u lua5.3 $<TARGET_FILE:TARGET_kafka>)
	ENDIF((${CMAKE_SYSTEM_NAME} STREQUAL "Windows") AND (${CMAKE_COMPILER_IS_GNUCC}))

	#----------------------------------------------------------------------------
	#
	# Build a package for the selected platform.
	#

	SET(CPACK_OUTPUT_FILE_PREFIX ${CMAKE_BINARY_DIR}/../../repository/org/muhkuh/lua/kafka/${PROJECT_VERSION})
	SET(CPACK_INCLUDE_TOPLEVEL_DIRECTORY FALSE)
	SET(CPACK_PACKAGE_FILE_NAME "lua${BUILDCFG_LUA_VERSION}-kafka-${PROJECT_VERSION}-${JONCHKI_PLATFORM}")

	SET(CPACK_GENERATOR "TXZ")
	INCLUDE(CPack)

	IF(${JONCHKI_PLATFORM_DIST_ID} STREQUAL "windows")
		# Package a windows build.

		INSTALL(FILES $<TARGET_FILE:TARGET_kafka>
		        DESTINATION lua_plugins)

		CONFIGURE_FILE(installer/lua${BUILDCFG_LUA_VERSION}/install_windows.lua
		               ${CMAKE_CURRENT_BINARY_DIR}/package/install.lua
		               @ONLY)

		INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/package/install.lua
		        DESTINATION .)
	ELSE(${JONCHKI_PLATFORM_DIST_ID} STREQUAL "windows")
		# Package a linux build.

		INSTALL(FILES $<TARGET_FILE:TARGET_kafka>
		        DESTINATION lua_plugins)

		CONFIGURE_FILE(installer/lua${BUILDCFG_LUA_VERSION}/install_linux.lua
		               ${CMAKE_CURRENT_BINARY_DIR}/package/install.lua
		               @ONLY)

		INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/package/install.lua
		        DESTINATION .)
	ENDIF(${JONCHKI_PLATFORM_DIST_ID} STREQUAL "windows")

	CONFIGURE_FILE(installer/lua${BUILDCFG_LUA_VERSION}/kafka.xml
	               ${CPACK_OUTPUT_FILE_PREFIX}/lua${BUILDCFG_LUA_VERSION}-kafka-${PROJECT_VERSION}-${JONCHKI_PLATFORM}.xml
	               @ONLY)


	#-----------------------------------------------------------------------------
	#
	# Create a custom target named "pack".
	# It builds the package and creates the hashes.
	#
	ADD_CUSTOM_TARGET(pack
	                  COMMAND ${CMAKE_MAKE_PROGRAM}
	                  COMMAND ${CMAKE_MAKE_PROGRAM} package
	                  COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_HOME_DIRECTORY}/cmake/tools/generate_hash.py ${CPACK_OUTPUT_FILE_PREFIX}/lua${BUILDCFG_LUA_VERSION}-kafka-${PROJECT_VERSION}-${JONCHKI_PLATFORM}.xml
	                  COMMAND "${PYTHON_EXECUTABLE}" ${CMAKE_HOME_DIRECTORY}/cmake/tools/generate_hash.py ${CPACK_OUTPUT_FILE_PREFIX}/lua${BUILDCFG_LUA_VERSION}-kafka-${PROJECT_VERSION}-${JONCHKI_PLATFORM}.tar.xz
	                  COMMENT "Pack the artifact and generate the hashes...")


	#-----------------------------------------------------------------------------
	#
	# Filter the pom.xml file.
	#
	SET(POM_ARTIFACT_ID  "lua${BUILDCFG_LUA_VERSION}-kafka")
	SET(POM_GROUP_ID     "com.github.mozilla-services.lua_sandbox_extensions")
	SET(POM_VERSION      "${PROJECT_VERSION}")
	SET(POM_PACKAGING    "tar.xz")
	CONFIGURE_FILE(installer/lua${BUILDCFG_LUA_VERSION}/pom.xml
	               ${CPACK_OUTPUT_FILE_PREFIX}/lua${BUILDCFG_LUA_VERSION}-kafka-${PROJECT_VERSION}.pom
	               @ONLY)
ENDIF((${BUILDCFG_ONLY_JONCHKI_CFG} STREQUAL "OFF"))
