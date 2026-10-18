# Define minimal required version of CMake.
cmake_minimum_required(VERSION "3.25")

# Project definition
project(
	SafeHaven_UDP_Client
	VERSION 1.0
	LANGUAGES C CXX ASM
)

# Include the definition of the slc_SafeHaven_UDP_Client target,
# which contains the content of the SLC project
include(SafeHaven_UDP_Client.cmake)

add_executable(SafeHaven_UDP_Client
    # Add additional sources here
)

target_include_directories(SafeHaven_UDP_Client PUBLIC
    # Add additional include paths here
)

target_compile_definitions(SafeHaven_UDP_Client PUBLIC
    # Add additional macros here
)

target_compile_options(SafeHaven_UDP_Client PUBLIC
    # Set additional compiler flags here
)

target_link_options(SafeHaven_UDP_Client PUBLIC
    # Set additional linker flags here
)

# Link with the content defined in the SLC project
target_link_libraries(SafeHaven_UDP_Client PRIVATE
    slc
)

# Include managed project content if available
include(SafeHaven_UDP_Client_project.cmake OPTIONAL RESULT_VARIABLE managed_project)
if(managed_project)
    message(STATUS "Using managed project content from ${managed_project}")
endif()

# Force the gcc linker command
set_target_properties(SafeHaven_UDP_Client PROPERTIES LINKER_LANGUAGE C)

# Create .bin, .hex and .s37 artifacts after building the project
add_custom_command(TARGET SafeHaven_UDP_Client
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_SREC_CMD} "$<TARGET_FILE:SafeHaven_UDP_Client>" "$<TARGET_FILE_DIR:SafeHaven_UDP_Client>/$<TARGET_FILE_BASE_NAME:SafeHaven_UDP_Client>.s37"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_IHEX_CMD} "$<TARGET_FILE:SafeHaven_UDP_Client>" "$<TARGET_FILE_DIR:SafeHaven_UDP_Client>/$<TARGET_FILE_BASE_NAME:SafeHaven_UDP_Client>.hex"
    COMMAND ${CMAKE_OBJCOPY} ${OBJCOPY_BIN_CMD}  "$<TARGET_FILE:SafeHaven_UDP_Client>" "$<TARGET_FILE_DIR:SafeHaven_UDP_Client>/$<TARGET_FILE_BASE_NAME:SafeHaven_UDP_Client>.bin" 
)

# Run post-build pipeline to perform additional post-processing
if(post_build_command)
add_custom_command(TARGET SafeHaven_UDP_Client
    POST_BUILD
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/..
    COMMAND ${post_build_command}
)
endif()