# =============================================================================
#  CMakeLists.txt  ——  告诉 CMake "这个项目由哪些文件组成、依赖什么库"
# -----------------------------------------------------------------------------
#  怎么用（在项目根目录）：
#      mkdir -p build
#      cd build
#      cmake ..
#      make -j
#      ./lightbar ../data/armor.avi
#
#  注意：cmake 一旦成功生成过，之后改源码只要重新 make 就行，
#        不用每次重新 cmake。
# =============================================================================

# cmake 的最低版本要求。写 3.10 是为了兼容 Ubuntu 18.04 自带的 cmake。
cmake_minimum_required(VERSION 3.10)

# 项目名。它不影响生成的程序名，只是给这个工程起个名字。
project(lightbar LANGUAGES CXX)

# 用 C++14 标准。OpenCV 4.x 的示例基本都是 C++11/14。
# 为什么不用更新的标准？因为队伍里的编译环境要一致，越保守越不容易出问题。
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认用 Release（开优化，跑得快）。
# 如果你要调试（gdb 单步），改成 Debug。
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# -----------------------------------------------------------------------------
#  找到 OpenCV
# -----------------------------------------------------------------------------
#  find_package 会去找一个叫 OpenCVConfig.cmake 的文件。
#  它可能藏在 /usr/local/opencv4.5.4/lib/cmake/opencv4/ 下。
#  如果 CMake 报 "Could not find a package configuration file provided by OpenCV"，
#  就是没找到这个文件 —— 下面这段会自动帮你指路。
#
#  如果你把 OpenCV 装在别的地方，只改这一行路径就行。
set(OPENCV_INSTALL_PREFIX "/usr/local/opencv4.5.4")

if(EXISTS "${OPENCV_INSTALL_PREFIX}/lib/cmake/opencv4/OpenCVConfig.cmake")
    # 告诉 find_package "去这个目录找配置文件"
    set(OpenCV_DIR "${OPENCV_INSTALL_PREFIX}/lib/cmake/opencv4")
endif()

# REQUIRED 表示"找不到就报错停下"，而不是悄悄地继续编译然后报一堆莫名其妙的错。
find_package(OpenCV 4 REQUIRED)

# 把找到的信息打印出来。cmake .. 的时候你会看到这几行，
# 这就是"OpenCV 到底找到没有、找到的是哪个版本"的证据。
message(STATUS "OpenCV 版本: ${OpenCV_VERSION}")
message(STATUS "OpenCV 头文件: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV 库: ${OpenCV_LIBS}")

# -----------------------------------------------------------------------------
#  生成可执行文件
# -----------------------------------------------------------------------------
#  第一个参数 lightbar 是生成出来的程序名；
#  后面列出所有需要编译的 .cpp 文件（注意：头文件不用列，它们是被 include 的）。
#  少写一个 .cpp，链接时就会报 "undefined reference"（找不到函数实现）。
add_executable(lightbar
    src/main.cpp
    src/ArmorDetector.cpp
    src/PoseSolver.cpp
)

# -----------------------------------------------------------------------------
#  头文件搜索路径 + 库链接
# -----------------------------------------------------------------------------
#  原文说的"三步"，对应下面两条 + 上面的 find_package：
#    ① 搜索外部库          → find_package(OpenCV REQUIRED)
#    ② 指定头文件搜索路径  → target_include_directories(...)
#    ③ 添加库链接          → target_link_libraries(...)
#
#  include 是自己的头文件目录（这样代码里能写 #include "lightbar/Config.h"）。
#  少了 ②，编译器会报 fatal error: opencv2/opencv.hpp: No such file or directory
#  少了 ③，链接器会报 undefined reference to `cv::imshow(...)`
target_include_directories(lightbar PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(lightbar PRIVATE
    ${OpenCV_LIBS}
)