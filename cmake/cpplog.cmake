set(DISABLE_UNINSTALL_TARGET TRUE)
	# TODO disabled for now, since cpplog uninstall target conflicts with VC4C uninstall target
	include(FetchContent)
	FetchContent_Declare(cpplog GIT_REPOSITORY https://github.com/doe300/cpplog.git GIT_TAG v0.6)
	FetchContent_GetProperties(cpplog)
	if(NOT cpplog_POPULATED)
	    FetchContent_Populate(cpplog)
	
	    # Примените патч
	    execute_process(
	        COMMAND git apply ${CMAKE_SOURCE_DIR}/cmake/disable_uninstall.patch
	        WORKING_DIRECTORY ${cpplog_SOURCE_DIR}
	    )
	
	    add_subdirectory(${cpplog_SOURCE_DIR} ${cpplog_BINARY_DIR})
	endif()
	# CMake configuration flags for cpplog project
	set(CPPLOG_NAMESPACE logging)
	set(CPPLOG_CUSTOM_LOGGER true)
	FetchContent_MakeAvailable(cpplog)
	FetchContent_GetProperties(cpplog BINARY_DIR CPPLOG_BINARY_DIR)
	FetchContent_GetProperties(cpplog SOURCE_DIR CPPLOG_SOURCE_DIR)
	
	add_library(cpplog-dependencies STATIC IMPORTED)
	set(cpplog_LIBS cpplog-static)
