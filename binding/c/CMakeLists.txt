set(PROJECT_BINDING_NAME "atapp_c")
EchoWithColor(COLOR GREEN "-- Configure ${CMAKE_CURRENT_LIST_DIR}")

# ============ c binding ============

file(GLOB BINDING_SRC_LIST RELATIVE "${CMAKE_CURRENT_LIST_DIR}"
    ${CMAKE_CURRENT_LIST_DIR}/atframe/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/atframe/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/atframe/*.c
    ${CMAKE_CURRENT_LIST_DIR}/atframe/*.cxx
)
include_directories(${CMAKE_CURRENT_LIST_DIR})

if ( NOT MSVC AND NOT APPLE )
    add_definitions(-shared)
endif()

# ============ libatbus - src ============
# 保持和MSVC得导出行为一致
if( ${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
    add_definitions(-fvisibility=hidden)
elseif( ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    add_definitions(-fvisibility=hidden)
endif()

add_library(${PROJECT_BINDING_NAME} SHARED ${BINDING_SRC_LIST} ${PROJECT_3RD_PARTY_SRC_LIST})

target_link_libraries(${PROJECT_BINDING_NAME}
    ${PROJECT_LIB_LINK}
    atapp
    ${3RD_PARTY_LIBATBUS_LINK_NAME}
    ${3RD_PARTY_ATFRAME_UTILS_LINK_NAME}
    ${3RD_PARTY_LIBUV_LINK_NAME}
    ${COMPILER_OPTION_EXTERN_CXX_LIBS}
)

install(TARGETS ${PROJECT_BINDING_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES ${CMAKE_CURRENT_LIST_DIR}/atframe/libatapp_c.h
    DESTINATION include/atframe
)

install(FILES "${3RD_PARTY_ATFRAME_UTILS_INC_DIR}/config/compile_optimize.h"
    DESTINATION include/atframe/config
)
