cmake_minimum_required(VERSION 3.7.0)
cmake_policy(SET CMP0054 NEW)
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.13")
    cmake_policy(SET CMP0074 NEW)
endif()

project(libatapp)

set(LIBATAPP_VERSION_MAJOR "0")
set(LIBATAPP_VERSION_MINOR "5")
set(LIBATAPP_VERSION_PATCH "0")
set(LIBATAPP_VERSION "${LIBATAPP_VERSION_MAJOR}.${LIBATAPP_VERSION_MINOR}.${LIBATAPP_VERSION_PATCH}")

# 准备下载依赖组件
include ("${CMAKE_CURRENT_LIST_DIR}/3rd_party/atframe_utils/libatframe_utils.prepare.cmake")

########################################################################
# CMake 模块 (递归包含模块, 带颜色输出模块, 平台检测模块)
set (PROJECT_CMAKE_MODULE_DIR "${3RD_PARTY_ATFRAME_UTILS_PKG_DIR}/project/cmake")
list(APPEND CMAKE_MODULE_PATH "${PROJECT_CMAKE_MODULE_DIR}/modules")


include("${PROJECT_CMAKE_MODULE_DIR}/modules/IncludeDirectoryRecurse.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/modules/EchoWithColor.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/modules/FindConfigurePackage.cmake")

include("${CMAKE_CURRENT_LIST_DIR}/project/cmake/ProjectBuildOption.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/FindPlatform.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/ProjectTools.cmake")


#####################################################################
# 导入编译器和编译选项配置
include("${PROJECT_CMAKE_MODULE_DIR}/CompilerOption.cmake")
include("${PROJECT_CMAKE_MODULE_DIR}/TargetOption.cmake")
EchoWithColor(COLOR GREEN "-- Build Type: ${CMAKE_BUILD_TYPE}")

########################################################################
# 导入项目配置
## 导入所有 macro 定义
include_macro_recurse("${CMAKE_CURRENT_LIST_DIR}")

## 导入工程项目
if ( "${CMAKE_CURRENT_LIST_DIR}/3rd_party/atframe_utils/repo" STREQUAL ${3RD_PARTY_ATFRAME_UTILS_PKG_DIR})
    add_subdirectory(${3RD_PARTY_ATFRAME_UTILS_PKG_DIR})
endif()

if ( "${CMAKE_CURRENT_LIST_DIR}/3rd_party/libatbus/repo" STREQUAL ${3RD_PARTY_LIBATBUS_PKG_DIR})
    add_subdirectory(${3RD_PARTY_LIBATBUS_PKG_DIR})
endif()

set (LIBATAPP_MACRO_BUSID_TYPE ${ATBUS_MACRO_BUSID_TYPE})
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/include/atframe/atapp_version.h.in"
    "${CMAKE_CURRENT_LIST_DIR}/include/atframe/atapp_version.h"
    @ONLY
)

# mingw
unset(ATAPP_PACKED_ADDTIONAL_LIBS)
if (MINGW)
    EchoWithColor(COLOR GREEN "-- MinGW: custom add lib ws2_32,psapi,userenv,iphlpapi ")
    list(APPEND ATAPP_PACKED_ADDTIONAL_LIBS ws2_32 psapi userenv iphlpapi)
elseif (WIN32)
    EchoWithColor(COLOR GREEN "-- Win32: custom add lib psapi,userenv,iphlpapi ")
    list(APPEND ATAPP_PACKED_ADDTIONAL_LIBS psapi userenv iphlpapi)
endif()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/src")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/binding/c")

if (PROJECT_ENABLE_SAMPLE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/sample")
endif()

# if (PROJECT_ENABLE_UNITTEST)
#     include ("${3RD_PARTY_ATFRAME_UTILS_PKG_DIR}/test/test.utils-macro.cmake")
#     add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/test")
# endif()

## 生成文档和导入配置

# Install configuration
set(CMAKE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake" CACHE STRING "Directory relative to CMAKE_INSTALL to install the cmake configuration files")

include(CMakePackageConfigHelpers)
set(INCLUDE_INSTALL_DIR include)

file(MAKE_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake")

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/libatapp-config.cmake.in"
    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/libatapp-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_CMAKEDIR}
    PATH_VARS LIBATAPP_VERSION INCLUDE_INSTALL_DIR CMAKE_INSTALL_LIBDIR PROJECT_SOURCE_DIR
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

write_basic_package_version_file(
    "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/libatapp-config-version.cmake"
    VERSION ${LIBATAPP_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/libatapp-config.cmake" "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/cmake/libatapp-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_CMAKEDIR}
)