

EchoWithColor(COLOR GREEN "-- Configure Tools ${CMAKE_CURRENT_LIST_DIR}")

# ============ TOOLS - [...] ============
include_directories(${CMAKE_CURRENT_LIST_DIR})

# ================ multi thread ================
find_package(Threads)
if (CMAKE_USE_PTHREADS_INIT)
    add_definitions(-D_POSIX_MT_)
    list(APPEND COMPILER_OPTION_EXTERN_CXX_LIBS pthread)
endif ()

file(GLOB TOOLS_SRC_LIST
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.cxx
)

# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/tools")

foreach(TOOLS_SRC_FILE IN LISTS TOOLS_SRC_LIST)
    get_filename_component(TOOLS_SRC_BIN_NAME "${TOOLS_SRC_FILE}" NAME_WE)

    add_executable("${TOOLS_SRC_BIN_NAME}" ${TOOLS_SRC_FILE})
    target_link_libraries("${TOOLS_SRC_BIN_NAME}"
        ${PROJECT_LIB_LINK}
        atapp_packed
        ${3RD_PARTY_LIBATBUS_LINK_NAME}
        ${3RD_PARTY_ATFRAME_UTILS_LINK_NAME}
        ${3RD_PARTY_LIBUV_LINK_NAME}
        ${ATAPP_PACKED_ADDTIONAL_LIBS}
        ${COMPILER_OPTION_EXTERN_CXX_LIBS}
	)

	install(TARGETS "${TOOLS_SRC_BIN_NAME}"
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

endforeach()
