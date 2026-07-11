cmake_minimum_required(VERSION 3.10)
project(MiniRedisDesktop VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Qt5 检测 ──
find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
message(STATUS "Qt5 version: ${Qt5_VERSION}")

# ── 头文件路径 ──
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── 源文件 ──
set(SOURCES
    src/main.cpp
    src/log.cpp
    src/redis_client.cpp
    src/server_manager.cpp
    src/mainwindow.cpp
    src/key_browser.cpp
    src/value_viewer.cpp
    src/console_panel.cpp
    src/stats_dashboard.cpp
    src/business_data.cpp
    src/business_panel.cpp
    src/monitor_panel.cpp
)

set(HEADERS
    include/log.h
    include/resp_codec.h
    include/redis_client.h
    include/server_manager.h
    include/mainwindow.h
    include/key_browser.h
    include/value_viewer.h
    include/console_panel.h
    include/stats_dashboard.h
    include/business_data.h
    include/business_panel.h
    include/monitor_panel.h
)

# ── 可执行文件输出到 build/bin/（避免与子目录同名冲突）──
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ── 可执行文件 ──
add_executable(MiniRedisDesktop ${SOURCES} ${HEADERS})

target_link_libraries(MiniRedisDesktop
    Qt5::Widgets
    Qt5::Network
    pthread
)

# ── mini_redis 内嵌服务器 ──
add_subdirectory(mini_redis)

# ── 安装规则 ──
install(TARGETS MiniRedisDesktop DESTINATION bin)
install(TARGETS mini_redis DESTINATION bin)
