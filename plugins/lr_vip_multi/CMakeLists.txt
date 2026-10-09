# lr_vip_multi — Plugify C++ port of the Pisex LR module "[LR][VIP] Multi"
# (ref/src/cs2-lvl_ranks_modules/LR_VIP_Multi).
#
#   lr_vip_multi_core       engine-free core (the kill multiplier; also used by
#                           tests/vip_multi_tests.cpp)
#   plugify-lr_vip_multi    the plugin shared library (entry "bin/lr_vip_multi")

set(LR_VIP_MULTI_VERSION "1.0.0")

# --- core ---------------------------------------------------------------------
add_library(lr_vip_multi_core STATIC src/vip_multi_core.cpp)
target_include_directories(lr_vip_multi_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# pisexcompat brings the KeyValues parser and, through its own PUBLIC link, the s2sdk binding
# globals (they stay null in the tests, which never call into s2sdk).
target_link_libraries(lr_vip_multi_core PUBLIC pisexcompat)
target_compile_options(lr_vip_multi_core PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(lr_vip_multi_core PUBLIC _GLIBCXX_USE_CXX11_ABI=1)

# --- generated consumer bindings -----------------------------------------------
# The __levels_ranks_* (levels_ranks) and __vip_* (vip) globals have to be compiled into every
# consumer plugin. `lr_bindings` is defined by plugins/levels_ranks (sorts before this directory);
# `vip_bindings` is defined by plugins/vip, which sorts AFTER lr_vip_multi, so the fallback target
# below (a distinct name - plugins/vip would otherwise define vip_bindings twice) is the one this
# plugin normally uses.
if(NOT TARGET lr_bindings AND EXISTS
        "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include/external/plugify/include/levels_ranks/levels_ranks.cpp")
    add_library(lr_bindings OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include/external/plugify/include/levels_ranks/levels_ranks.cpp")
    target_include_directories(lr_bindings SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(lr_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
endif()

if(TARGET vip_bindings)
    set(LR_VIP_MULTI_VIP_BINDINGS "$<TARGET_OBJECTS:vip_bindings>")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    add_library(vip_bindings_lr_vip_multi OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    target_include_directories(vip_bindings_lr_vip_multi SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(vip_bindings_lr_vip_multi PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
    set(LR_VIP_MULTI_VIP_BINDINGS "$<TARGET_OBJECTS:vip_bindings_lr_vip_multi>")
else()
    message(FATAL_ERROR "lr_vip_multi needs the generated vip consumer bindings (plugins/vip/include/external/plugify/include/vip/vip.cpp)")
endif()

# --- plugin -------------------------------------------------------------------
add_library(plugify-lr_vip_multi SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:lr_bindings>
        ${LR_VIP_MULTI_VIP_BINDINGS}
)
target_include_directories(plugify-lr_vip_multi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated lr/vip/plg/s2sdk headers and the two hand-written consumer
# wrappers must not fail -Werror on our sources.
target_include_directories(plugify-lr_vip_multi SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include"
        "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(plugify-lr_vip_multi PRIVATE
        lr_vip_multi_core
        -static-libstdc++ -static-libgcc
)
target_compile_options(plugify-lr_vip_multi PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(plugify-lr_vip_multi PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="lr_vip_multi"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1
)
set_target_properties(plugify-lr_vip_multi PROPERTIES OUTPUT_NAME lr_vip_multi)

if(UNIX AND NOT APPLE)
    target_link_options(plugify-lr_vip_multi PRIVATE
            "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/sym/version_script.lds")
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/lr_vip_multi.pplugin.in"
        "${CMAKE_CURRENT_BINARY_DIR}/lr_vip_multi.pplugin"
)

# --- tests ---------------------------------------------------------------------
# Engine-free tests of the multiplier (build/tests so ../test.sh runs them).
add_executable(lr_vip_multi_tests tests/vip_multi_tests.cpp)
target_link_libraries(lr_vip_multi_tests PRIVATE lr_vip_multi_core -static-libstdc++ -static-libgcc)
target_compile_options(lr_vip_multi_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(lr_vip_multi_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        LR_VIP_MULTI_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/ref/prod-27016/addons/configs")
set_target_properties(lr_vip_multi_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
