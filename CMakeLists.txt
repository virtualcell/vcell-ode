#############################################
#
#  Top Level CMake File
#
##############################################
cmake_minimum_required(VERSION 3.13)
project(vcell-ode-numerics)
set(CMAKE_CXX_STANDARD 17)
enable_language(CXX)
enable_language(C)
#enable_language(Fortran)

#############################################
#  If this isn't explicitly set in cmake 3.x.x, we get warnings
##############################################
if(POLICY CMP0057)
	cmake_policy(SET CMP0057 NEW)
endif()

#############################################
#  Display extra CMAKE info while configuring
##############################################
option(OPTION_EXTRA_CONFIG_INFO "Print useful cmake debug info while configuring" off)

#############################################
#
#  Build 64bit binaries on Mac and target Macos 10.7 or later
#
#	set(CMAKE_Fortran_OSX_DEPLOYMENT_TARGET_FLAG "-mmacosx-version-min=10.7" CACHE PATH "")
#	set(CMAKE_OSX_DEPLOYMENT_TARGET "10.7" CACHE PATH "")
#
#  Choose 32bit or 64bit target arch on Linux
#
##############################################
if (LINUX)
	option(LINUX_32bit_BINARIES "Build 32bit Linux BINARIES" OFF)
	option(LINUX_64bit_BINARIES "Build 64bit Linux BINARIES" ON)

	if (LINUX_32bit_BINARIES AND LINUX_64bit_BINARIES)
		message(FATAL_ERROR "It is required to select either 32bit or 64bit Linux Binaries, not both")
	endif()

	if (LINUX_32bit_BINARIES) 
		set (ARCH_64bit FALSE)
		set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
		set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
#		set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -m32")
	endif()
	
	if (LINUX_64bit_BINARIES) 
		set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m64")
		set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m64")
#		set (CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} -m64")
	endif()
endif()

#include (LocalJNI.cmake)
set(RULE_MESSAGES OFF)
set(CMAKE_COLOR_MAKEFILE OFF)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/modules/")

include(GetGitRevisionDescription)
git_describe(GIT_DESCRIBE)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#include (VCellCMakeShowVariables.txt)

if(DEFINED CMAKE_BUILD_TYPE)
   SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel.")
else()
   SET(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: None(CMAKE_CXX_FLAGS or CMAKE_C_FLAGS used) Debug Release RelWithDebInfo MinSizeRel.")
endif()

#include (CMakeTestCCompiler)
#include (CheckCSourceCompiles)
#include (CheckCXXSourceCompiles)
#include (CheckStructHasMember)
#include (CheckLibraryExists)
#include (CheckFunctionExists)
#include (CheckCCompilerFlag)
#include (CheckCSourceRuns)
#include (CheckSymbolExists)
#include (CheckTypeSize)
include (FindZLIB)

option(OPTION_TARGET_MESSAGING "Messaging (requires libcurl)" off)
option(OPTION_TARGET_DOCS "Generate Doxygen documentation" on)
 
                  
if (${OPTION_TARGET_DOCS})
#	if (DOXYGEN_FOUND AND ${CMAKE_VERSION} GREATER_EQUAL 3.9)
#		find_package(Doxygen
#				REQUIRED dot
#				OPTIONAL_COMPONENTS mscgen dia)
#		set(DOXYGEN_GENERATE_HTML YES)
#		set(DOXYGEN_GENERATE_MAN NO)
#		doxygen_add_docs(
#   			doxygen
#    		${PROJECT_SOURCE_DIR}
#    		COMMENT "Generate man pages"
#    	)
#	else()
		add_custom_target(graphviz ALL
                  "${CMAKE_COMMAND}" "--graphviz=dependencies.dot" .
                  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
#    endif()
endif()

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.7" CACHE STRING "Choose minimum deploy target for Macos machines")
endif()

set(HDF5_BUILD_CPP_LIB OFF CACHE BOOL "" )

set(LINUX FALSE)
if (${CMAKE_SYSTEM_NAME} MATCHES Linux)
	set(LINUX TRUE)
endif()

set(WINDOWS FALSE)
if (${CMAKE_SYSTEM_NAME} MATCHES Windows)
	set(WINDOWS TRUE)
	set(WIN32 TRUE)
	set(MINGW TRUE)
endif()

set (ARCH_64bit FALSE)
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	set (ARCH_64bit TRUE)
endif()

if (NOT APPLE AND NOT LINUX AND NOT WINDOWS)
	message(FATAL_ERROR "Unsupported Operating System")
endif()

#---------------------------
#  IDE SUPPORT
#---------------------------
if (WINDOWS)
	set(NETBEANS_WINDOWS TRUE)
	set(CMAKE_RC_FLAGS "-DGCC_WINDRES")
endif(WINDOWS)

#add_definitions(-DFORTRAN_UNDERSCORE)

#############################################
# installation directories
#############################################
if (NOT OPTION_TARGET_MESSAGING)
	set(OPTION_EXE_DIRECTORY "bin" CACHE PATH "installation directory")
else (NOT OPTION_TARGET_MESSAGING)
	set(OPTION_EXE_DIRECTORY "../bin" CACHE PATH "installation directory")
endif (NOT OPTION_TARGET_MESSAGING)

set(OPTION_LIB_DIRECTORY "lib" CACHE PATH "library directory")
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}")


add_definitions(-DUNIX)


if (OPTION_TARGET_MESSAGING)
	#don't need graphics for server side solvers
	set(OPTION_USE_OPENGL OFF CACHE BOOL "")
	
	find_package(CURL REQUIRED)
	set(THREADS_PREFER_PTHREAD_FLAG ON)
	find_package(Threads REQUIRED)
	add_definitions(-DUSE_MESSAGING)
endif()

######################################
#
# Add subdirectories
# 
#######################################

add_subdirectory(VCellMessaging)
message(STATUS "adding ExpressionParser")
add_subdirectory(ExpressionParser)
message(STATUS "adding sundials")
add_subdirectory(sundials)
add_subdirectory(IDAWin)

set(DOWNLOAD_EXTRACT_TIMESTAMP true)
cmake_policy(SET CMP0135 NEW)
#############################################
#
#  Declare and install dependency: Google-test
#
##############################################
include(FetchContent)
FetchContent_Declare(
		googletest
		GIT_REPOSITORY https://github.com/google/googletest.git
		GIT_TAG          v1.17.0
)
if (WINDOWS)
	# For Windows: Prevent overriding the parent project's compiler/linker settings
	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(googletest)
enable_testing()

#############################################
#
#  Display all sorts of detailed debug information
#
##############################################
if (OPTION_EXTRA_CONFIG_INFO)
	get_cmake_property(_variableNames VARIABLES)
	list (SORT _variableNames)
	foreach (_variableName ${_variableNames})
		message(STATUS "${_variableName}=${${_variableName}}")
	endforeach()

	include(CMakePrintHelpers)
	cmake_print_variables(OPTION_TARGET_MESSAGING OPTION_TARGET_DOCS)
#	cmake_print_variables(CMAKE_CXX_FLAGS CMAKE_C_FLAGS CMAKE_Fortran_FLAGS)
	cmake_print_variables(CMAKE_CXX_FLAGS CMAKE_C_FLAGS)
	cmake_print_variables(CMAKE_SYSTEM_NAME WINDOWS WIN32 MINGW APPLE ARCH_64bit ARCH_32bit)
#	cmake_print_variables(CMAKE_CPP_COMPILER CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_Fortran_COMPILER)
	cmake_print_variables(CMAKE_CPP_COMPILER CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
	cmake_print_variables(HAVE_ZLIB)
endif ()

