if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.10")
    include_guard(GLOBAL)
endif()

# =========== libatbus ==================
if (NOT 3RD_PARTY_LIBATBUS_BASE_DIR)
    set (3RD_PARTY_LIBATBUS_BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()

if (LIBATBUS_ROOT)
    set (3RD_PARTY_LIBATBUS_PKG_DIR ${LIBATBUS_ROOT})
else()
    set (3RD_PARTY_LIBATBUS_PKG_DIR "${3RD_PARTY_LIBATBUS_BASE_DIR}/repo")
    if(NOT EXISTS ${3RD_PARTY_LIBATBUS_PKG_DIR})
        find_package(Git)
        execute_process(COMMAND ${GIT_EXECUTABLE} clone --depth=1 "https://github.com/atframework/libatbus.git" ${3RD_PARTY_LIBATBUS_PKG_DIR}
            WORKING_DIRECTORY ${3RD_PARTY_LIBATBUS_BASE_DIR}
        )
    endif()
endif()

set (3RD_PARTY_LIBATBUS_INC_DIR "${3RD_PARTY_LIBATBUS_PKG_DIR}/include")
set (3RD_PARTY_LIBATBUS_SRC_DIR "${3RD_PARTY_LIBATBUS_PKG_DIR}/src")
set (3RD_PARTY_LIBATBUS_LINK_NAME atbus)

include_directories(${3RD_PARTY_LIBATBUS_INC_DIR})