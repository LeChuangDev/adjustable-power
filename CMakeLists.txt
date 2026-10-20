cmake_minimum_required(VERSION 3.16)

# 额外组件目录：屏幕第三方组件
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_SOURCE_DIR}/components/lcd"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(adjustable-power)