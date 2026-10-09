cmake_minimum_required(VERSION 3.20)

# mouselnk-plus —— Windows 鼠标手势 / 鼠标增强工具
#
# 目标产物：单文件、无 VC++ 运行库依赖（静态 CRT /MT）的便携 exe，常驻系统托盘。
# 工具链：C++20 + MSVC v143 + Windows SDK（作者规格指定，见 temp/input/mouseinc-author-spec.md）。

project(mouselnk-plus VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "本工程按作者规格使用 MSVC（v143）+ Windows SDK，请用 Visual Studio 17 2022 生成器配置。")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "作者规格：首先支持 Windows 10/11 x64。请加 -A x64。")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# rc.exe 默认按系统代码页读取 .rc，而本工程的 .rc 与要嵌入的资源（默认配置 JSON）
# 都是 UTF-8 无 BOM。不指定编码时中文会被误解析 —— 实测过一次：中文注释把紧随其后的
# #define 行吞掉，导致 IDI_APP 未展开、资源变成了字符串名 "IDI_APP" 而不是序号 101。
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /c65001 /nologo")

# 静态 CRT：这是「单文件、无运行库依赖」的实现方式，不要改成动态 CRT。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# WTL 10.0（MS-PL），已 vendored 到 third_party/wtl，不改其源码。
set(WTL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wtl/Include")
if(NOT EXISTS "${WTL_INCLUDE_DIR}/atlapp.h")
    message(FATAL_ERROR "缺少 WTL 头文件：${WTL_INCLUDE_DIR}/atlapp.h")
endif()

add_executable(mouselnk-plus WIN32
    src/main.cpp
    src/App.cpp
    src/Tray.cpp
    src/Config.cpp
    src/Log.cpp
    src/GestureState.cpp
    src/Input.cpp
    src/app.rc
    # manifest 作为源文件交给 CMake，由链接器合并进唯一的 MANIFEST 资源
    # （声明 Common Controls v6、PerMonitorV2 DPI 感知、asInvoker）。
    src/app.manifest
)

# nlohmann/json 用 SYSTEM 引入：这库有近 1MB 单头文件，/W4 下会产生大量
# 来自第三方代码的告警，屏蔽掉才能让 /W4 对我们的代码保持有效。
target_include_directories(mouselnk-plus PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${WTL_INCLUDE_DIR}"
)
target_include_directories(mouselnk-plus SYSTEM PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_compile_definitions(mouselnk-plus PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    STRICT
)

target_compile_options(mouselnk-plus PRIVATE
    /W4                 # 高警告等级
    /permissive-        # 严格一致性模式
    /Zc:__cplusplus     # 让 __cplusplus 反映真实标准版本（MSVC 默认恒为 199711L）
    /Zc:preprocessor    # 符合标准的预处理器
    /utf-8              # 源码与执行字符集均为 UTF-8
    /EHsc
)

# 发布版按体积优化：手势工具的体积是产品定义的一部分（见 CONTEXT.md 硬约束 9）。
set_target_properties(mouselnk-plus PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON
)

target_link_libraries(mouselnk-plus PRIVATE
    comctl32    # 常用控件 v6（manifest 已声明）
    shell32     # Shell_NotifyIcon / ShellExecuteEx / 资源管理器定位
    user32
    gdi32
    advapi32    # 注册表（开机启动项）
)

# 图标与默认配置都被 .rc 引用，改它们时要触发重新编译资源，否则会静默沿用旧内容。
# 实测本机制在 Ninja 生成器下生效：改图标后构建输出会出现资源编译步骤（如 [2/2]）。
# 判断资源是否真的重编要看构建的步骤数，不能只看最后一行 Linking。
set_property(SOURCE src/app.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/app_paused.ico;${CMAKE_CURRENT_SOURCE_DIR}/resources/default-config.json"
)
