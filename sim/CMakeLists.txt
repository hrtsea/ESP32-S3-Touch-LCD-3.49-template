# sim/CMakeLists.txt - PC LVGL 模拟器构建
#
# 工具链：MinGW (gcc 15.x) + SDL2 + LVGL 8.3.11
# 构建：cd sim && mkdir build && cd build && cmake -G "MinGW Makefiles" .. && mingw32-make -j8
#
# 三层架构：
#   sim_stubs     - ESP-IDF 头文件 stub + wifi_cfg_* mock + client create stubs
#   sim_overrides - 替换原 main/ 中硬件相关模块（disp_driver/event_bus/fan_control/...）
#   nas_monitor_sim - 引用 main/ 下 UI 源码 + sim/main.c，链接上述两库

cmake_minimum_required(VERSION 3.12.4)
project(nas_monitor_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 不使用 MSVC，强制 MinGW 风格
if(MSVC)
    message(FATAL_ERROR "Use MinGW Makefiles generator: cmake -G \"MinGW Makefiles\" ..")
endif()

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ============================================================
# 定位 SDL2（MinGW 路径：C:\msys64\mingw64）
# ============================================================
find_path(SDL2_INCLUDE_DIR
    NAMES SDL2/SDL.h
    PATHS
        C:/msys64/mingw64/include
        $ENV{SDL2_INCLUDE_DIR}
    NO_DEFAULT_PATH
)
find_library(SDL2_LIBRARY
    NAMES SDL2
    PATHS
        C:/msys64/mingw64/lib
        $ENV{SDL2_LIBRARY_DIR}
    NO_DEFAULT_PATH
)
find_library(SDL2_MAIN_LIBRARY
    NAMES SDL2main
    PATHS
        C:/msys64/mingw64/lib
        $ENV{SDL2_LIBRARY_DIR}
    NO_DEFAULT_PATH
)

if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY OR NOT SDL2_MAIN_LIBRARY)
    message(FATAL_ERROR
        "SDL2 not found. Expect: C:/msys64/mingw64/include/SDL2/SDL.h, libSDL2.a, libSDL2main.a")
endif()

message(STATUS "SDL2 include: ${SDL2_INCLUDE_DIR}")
message(STATUS "SDL2 lib:     ${SDL2_LIBRARY}")
message(STATUS "SDL2 main:    ${SDL2_MAIN_LIBRARY}")

# ============================================================
# 路径常量
# ============================================================
set(SIM_DIR    ${CMAKE_CURRENT_LIST_DIR})
set(MAIN_DIR   ${CMAKE_CURRENT_LIST_DIR}/../main)
set(LVGL_DIR   ${CMAKE_CURRENT_LIST_DIR}/../components/lvgl)
set(WIFI_CFG_DIR ${CMAKE_CURRENT_LIST_DIR}/../managed_components/thorrak__esp_wifi_config/include)

# ============================================================
# LVGL（PC 自定义构建，使用 sim/lv_conf.h）
# ============================================================
set(LV_CONF_PATH ${SIM_DIR}/lv_conf.h CACHE PATH "Path to lv_conf.h" FORCE)
add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl_build)

# ============================================================
# 1) sim_stubs 静态库
# ============================================================
add_library(sim_stubs STATIC
    ${SIM_DIR}/stubs/stubs.c
)
target_include_directories(sim_stubs PUBLIC
    ${SIM_DIR}/stubs
    ${SIM_DIR}/stubs/bsp
    # stubs.c 引用项目数据/配置/驱动头文件
    ${MAIN_DIR}
    ${MAIN_DIR}/ui
    ${MAIN_DIR}/drivers
    ${MAIN_DIR}/config
    ${MAIN_DIR}/utils
    ${MAIN_DIR}/data
    ${MAIN_DIR}/data/client
    ${MAIN_DIR}/network
    ${WIFI_CFG_DIR}
)

# ============================================================
# 2) sim_overrides 静态库
# ============================================================
add_library(sim_overrides STATIC
    ${SIM_DIR}/overrides/disp_driver.c
    ${SIM_DIR}/overrides/event_bus.c
    ${SIM_DIR}/overrides/bg_fetcher.c
    ${SIM_DIR}/overrides/hw_init.c
    ${SIM_DIR}/overrides/cli.c
    ${SIM_DIR}/overrides/nas_event_loop.c
    ${SIM_DIR}/overrides/sntp_manager.c
    ${SIM_DIR}/overrides/wifi_manager.c
    ${SIM_DIR}/overrides/fan_control.c
    ${SIM_DIR}/sdl/sdl_driver.c
)
target_include_directories(sim_overrides PUBLIC
    ${SIM_DIR}
    ${SIM_DIR}/sdl
)
target_link_libraries(sim_overrides PUBLIC
    sim_stubs
    lvgl
    ${SDL2_LIBRARY}
)

# ============================================================
# 3) nas_monitor_sim 可执行文件
#    引用 main/ 下的 UI 源码（不复制），覆盖硬件相关模块
# ============================================================
set(UI_SOURCES
    ${MAIN_DIR}/ui/ui.c
    ${MAIN_DIR}/ui/ui_helpers.c
    ${MAIN_DIR}/ui/ui_widgets.c
    ${MAIN_DIR}/ui/ui_events.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Boot.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Overview.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Storage.c
    ${MAIN_DIR}/ui/screens/ui_Screen_WifiConfig.c
    ${MAIN_DIR}/ui/screens/ui_Screen_DiskDetail.c
    ${MAIN_DIR}/ui/screens/ui_Screen_SystemDetail.c
    ${MAIN_DIR}/ui/screens/ui_Screen_SDCopy.c
    ${MAIN_DIR}/ui/screens/ui_Screen_NetDetail.c
    # Settings Tab 子模块
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings_WifiTab.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings_NasTab.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings_ScreenTab.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings_StationTab.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings_MusicTab.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings_RegionTab.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings_GuideTab.c
    ${MAIN_DIR}/ui/screens/ui_Screen_Settings_FanTab.c
    # 字体
    ${MAIN_DIR}/ui/fonts/font_cjk_14.c
    ${MAIN_DIR}/ui/fonts/font_jbmono_24.c
    ${MAIN_DIR}/ui/fonts/font_jbmono_48.c
    ${MAIN_DIR}/ui/fonts/font_jbmono_64.c
    ${MAIN_DIR}/ui/fonts/font_jbmono_96.c
    ${MAIN_DIR}/ui/fonts/ui_font_NotoSanThai16.c
    # 图片
    ${MAIN_DIR}/ui/images/ui_img_images_button_png.c
    ${MAIN_DIR}/ui/images/ui_img_images_globe_png.c
    ${MAIN_DIR}/ui/images/ui_img_images_timezone_png.c
)

set(DATA_SOURCES
    ${MAIN_DIR}/data/data_source.c
    ${MAIN_DIR}/data/client/mock_client.c
    # nas_data_json.c 需要 cJSON，sim 跳过（UI 不依赖 JSON 序列化）
)

set(UTILS_SOURCES
    ${MAIN_DIR}/utils/i18n.c
    ${MAIN_DIR}/utils/theme.c
    ${MAIN_DIR}/utils/format.c
)

# 直接编译原版的配置/事件总线（nvs stub 返回 NOT_FOUND 即可正常工作）
set(CONFIG_SOURCES
    ${MAIN_DIR}/config/app_cfg.c
)

add_executable(nas_monitor_sim
    ${SIM_DIR}/main.c
    ${UI_SOURCES}
    ${DATA_SOURCES}
    ${UTILS_SOURCES}
    ${CONFIG_SOURCES}
)

# include 路径顺序：sim stubs 优先于 managed_components，main 各子目录确保项目头解析
target_include_directories(nas_monitor_sim PRIVATE
    # sim stubs（必须在 wifi_cfg 之前，确保 esp_err.h/esp_wifi_types.h 等被 stub 覆盖）
    ${SIM_DIR}/stubs
    ${SIM_DIR}/stubs/bsp
    ${SIM_DIR}/stubs/clients
    # 项目 main 各子目录
    ${MAIN_DIR}
    ${MAIN_DIR}/ui
    ${MAIN_DIR}/drivers
    ${MAIN_DIR}/config
    ${MAIN_DIR}/utils
    ${MAIN_DIR}/data
    ${MAIN_DIR}/data/client
    ${MAIN_DIR}/network
    # sim 自身（main.c 引用 "sdl/sdl_driver.h"）
    ${SIM_DIR}
    # components 父目录：项目头使用 #include <lvgl/lvgl.h> 风格
    ${CMAKE_CURRENT_LIST_DIR}/../components
    # managed_components esp_wifi_config 原头
    ${WIFI_CFG_DIR}
)

target_link_libraries(nas_monitor_sim PRIVATE
    sim_overrides
    sim_stubs
    lvgl
    ${SDL2_LIBRARY}
)

# MinGW: 控制台模式（保留 stdout），SDL2main 提供入口包装
target_compile_options(nas_monitor_sim PRIVATE
    -O2
    -Wall
    -D_GNU_SOURCE                 # 让 MinGW stdlib.h 暴露 setenv/strcasecmp 等
    -Wno-unused-parameter
    -Wno-unused-but-set-variable
    -Wno-unused-variable
    -Wno-format
    -Wno-pointer-sign
    -Wno-incompatible-pointer-types
    -Wno-int-conversion
    -Wno-discarded-qualifiers
)
target_compile_options(sim_stubs PRIVATE
    -O2 -Wall -D_GNU_SOURCE -Wno-unused-parameter -Wno-unused-variable
)
target_compile_options(sim_overrides PRIVATE
    -O2 -Wall -D_GNU_SOURCE -Wno-unused-parameter -Wno-unused-variable
)

# Windows 链接：SDL2main 必须在 SDL2 之前
if(WIN32)
    target_link_options(nas_monitor_sim PRIVATE -mconsole)
endif()

# 复制 SDL2.dll 到输出目录（如果存在）
find_file(SDL2_DLL
    NAMES SDL2.dll
    PATHS C:/msys64/mingw64/bin
    NO_DEFAULT_PATH
)
if(SDL2_DLL)
    add_custom_command(TARGET nas_monitor_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${SDL2_DLL} $<TARGET_FILE_DIR:nas_monitor_sim>
        COMMENT "Copying SDL2.dll to output directory"
    )
endif()
