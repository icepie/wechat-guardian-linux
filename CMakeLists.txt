cmake_minimum_required(VERSION 3.20)
project(wechat_antirecall_linux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)

add_library(antirecall_core STATIC
    src/build_id.cpp
    src/inline_hook.cpp
    src/pattern.cpp
    src/process.cpp
)
target_include_directories(antirecall_core PUBLIC include)
target_link_libraries(antirecall_core PUBLIC Zydis)

add_library(wechat_antirecall SHARED
    src/runtime.cpp
)
target_link_libraries(wechat_antirecall PRIVATE antirecall_core dl)
set_target_properties(wechat_antirecall PROPERTIES OUTPUT_NAME wechat-antirecall)

add_executable(antirecall-inspect tools/inspect.cpp)
target_link_libraries(antirecall-inspect PRIVATE antirecall_core)

if(BUILD_TESTING)
    add_executable(antirecall-tests
        tests/test_main.cpp
    )
    target_link_libraries(antirecall-tests PRIVATE antirecall_core)
    add_test(NAME antirecall-tests COMMAND antirecall-tests)

    add_executable(inline-hook-test tests/hook_target.cpp)
    target_link_libraries(inline-hook-test PRIVATE antirecall_core)
    target_compile_options(inline-hook-test PRIVATE -fno-omit-frame-pointer -fno-optimize-sibling-calls)
    add_test(NAME inline-hook-test COMMAND inline-hook-test)

    add_test(NAME cli-test COMMAND ${CMAKE_SOURCE_DIR}/tests/test_cli.sh)
endif()
