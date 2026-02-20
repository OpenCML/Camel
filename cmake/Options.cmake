# Camel 项目编译选项
# 选项定义见 cmake/options.json，可用 npm run config 交互式配置
# 用户覆盖保存在 .camel-opts.json，build/debug/profile 会自动读取

include_guard(GLOBAL)

# 当使用 CAMEL_OPTIONS_TARGET（interface 库）时用 INTERFACE，否则用 PRIVATE
set(_opt_visibility PRIVATE)
set(_opt_target ${PROJECT_NAME})
if(DEFINED CAMEL_OPTIONS_TARGET)
    set(_opt_visibility INTERFACE)
    set(_opt_target ${CAMEL_OPTIONS_TARGET})
endif()

# 简单布尔选项: option(NAME) + target_compile_definitions(NAME=0|1)
macro(add_bool_option name description default)
    option(${name} ${description} ${default})
    if(${name})
        target_compile_definitions(${_opt_target} ${_opt_visibility} ${name}=1)
    else()
        target_compile_definitions(${_opt_target} ${_opt_visibility} ${name}=0)
    endif()
    message(STATUS "${name}=${${name}}")
endmacro()

# 带平台检查的选项: 仅在指定平台且 option=ON 时启用
# extra_on/off: 额外的 compile definitions（如 JIT_TARGET_X64=1）
macro(add_platform_option name description default platform_regex extra_on extra_off)
    option(${name} ${description} ${default})
    set(_enabled FALSE)
    if(${name} AND CMAKE_SYSTEM_PROCESSOR MATCHES ${platform_regex})
        set(_enabled TRUE)
    endif()
    if(_enabled)
        target_compile_definitions(${_opt_target} ${_opt_visibility} ${name}=1 ${extra_on})
        message(STATUS "${name}=ON")
    else()
        target_compile_definitions(${_opt_target} ${_opt_visibility} ${name}=0 ${extra_off})
        if(${name} AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES ${platform_regex})
            message(STATUS "${name}=OFF (platform ${CMAKE_SYSTEM_PROCESSOR})")
        else()
            message(STATUS "${name}=OFF")
        endif()
    endif()
endmacro()

# ========== FastVM 选项 ==========

message(STATUS "============ Build options: ============")

add_platform_option(
    ENABLE_FASTVM_JIT
    "Enable JIT compilation (x64 only)"
    ON
    "x86_64|AMD64|amd64|X86_64"
    "JIT_TARGET_X64=1"
    "JIT_TARGET_X64=0"
)

add_bool_option(ENABLE_FASTVM_COMPUTED_GOTO "Use computed goto in interpreter" ON)

message(STATUS "========================================")