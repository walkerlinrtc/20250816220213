cmake_minimum_required(VERSION 3.10)
project(RTMPClient VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# 检查是否为Linux平台
if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "This project only supports Linux platform")
endif()

# 查找依赖库
find_package(Threads REQUIRED)

# 查找spdlog
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    # 如果系统没有安装spdlog，使用FetchContent下载
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.12.0
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# 源文件
set(SOURCES
    main.cpp
    rtmp_client.cpp
    rtmp_logger.cpp
    config_parser.cpp
)

# 头文件
set(HEADERS
    rtmp_client.h
    config_parser.h
)

# 创建可执行文件
add_executable(rtmp_client ${SOURCES} ${HEADERS})

# 链接系统库和第三方库
target_link_libraries(rtmp_client 
    Threads::Threads
    spdlog::spdlog
)

# 设置输出目录
set_target_properties(rtmp_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 安装规则
install(TARGETS rtmp_client
    RUNTIME DESTINATION bin
)

# 安装头文件（如果需要作为库使用）
install(FILES ${HEADERS}
    DESTINATION include/rtmp_client
)

# 创建包配置
include(CMakePackageConfigHelpers)

# 设置版本信息
set_property(TARGET rtmp_client PROPERTY VERSION ${PROJECT_VERSION})
set_property(TARGET rtmp_client PROPERTY SOVERSION 1)

# 添加编译定义
target_compile_definitions(rtmp_client PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
)

# 打印构建信息
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ flags: ${CMAKE_CXX_FLAGS}")