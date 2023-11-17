cmake_minimum_required(VERSION 3.10)

# 设置项目名称
set(PROJECT_NAME Emile)
project(${PROJECT_NAME})

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDPDK REQUIRED libdpdk)

# 指定源文件
set(SOURCE_FILES ./src/main.c)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 添加 DPDK 的编译选项
target_include_directories(${PROJECT_NAME} PRIVATE ${LIBDPDK_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${LIBDPDK_CFLAGS_OTHER})

# 链接 DPDK 库
target_link_libraries(${PROJECT_NAME} ${LIBDPDK_LIBRARIES})
