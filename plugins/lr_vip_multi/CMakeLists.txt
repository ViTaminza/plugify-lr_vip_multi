# lr_vip_multi — Plugify C++ port of the Pisex LR module "[LR][VIP] Multi"
# (the original LR_VIP_Multi module).
#
# Pure s2sdk (docs/PLUGIN_STANDARD.md §7): no pisexcompat and no guard library. The only engine
# read is the "attacker" player slot of the game event the levels_ranks kill hook hands over; the
# map gate, the slot bounds and the s2sdk binding checks live in src/plugin.cpp.
#
#   lr_vip_multi_core       engine-free core (the kill multiplier; also used by
#                           tests/lr_vip_multi_tests.cpp)
#   plugify-lr_vip_multi    the plugin shared library (entry "bin/lr_vip_multi")

set(LR_VIP_MULTI_VERSION "1.1.1")

# --- core ---------------------------------------------------------------------
add_library(lr_vip_multi_core STATIC src/vip_multi_core.cpp)
target_include_directories(lr_vip_multi_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# Engine-free: the multiplier core has no plugify/s2sdk dependency (the test links only this).
target_compile_options(lr_vip_multi_core PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(lr_vip_multi_core PUBLIC _GLIBCXX_USE_CXX11_ABI=1)

# --- generated consumer bindings -----------------------------------------------
# The __levels_ranks_* (levels_ranks) and __vip_* (vip) globals have to be compiled into every
# consumer plugin. Both object libraries are standalone fallbacks for an exported repository
# (which has no sibling CMakeLists.txt). In the monorepo plugins/levels_ranks is configured first
# and defines `lr_bindings`, while plugins/vip sorts AFTER lr_vip_multi, so `vip_bindings` does not
# exist yet when this directory is configured - hence the distinct private target name
# `vip_bindings_lr_vip_multi` (plugins/vip would otherwise define vip_bindings twice).
if(TARGET lr_bindings)
    set(LR_VIP_MULTI_LR_BINDING_TARGET lr_bindings)
elseif(EXISTS
        "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include/external/plugify/include/levels_ranks/levels_ranks.cpp")
    add_library(lr_bindings OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include/external/plugify/include/levels_ranks/levels_ranks.cpp")
    target_include_directories(lr_bindings SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(lr_bindings PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
    set(LR_VIP_MULTI_LR_BINDING_TARGET lr_bindings)
else()
    message(FATAL_ERROR "lr_vip_multi needs the generated levels_ranks consumer bindings (plugins/levels_ranks/include/external/plugify/include/levels_ranks/levels_ranks.cpp)")
endif()

if(TARGET vip_bindings)
    set(LR_VIP_MULTI_VIP_BINDING_TARGET vip_bindings)
elseif(EXISTS "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    add_library(vip_bindings_lr_vip_multi OBJECT
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include/vip/vip.cpp")
    target_include_directories(vip_bindings_lr_vip_multi SYSTEM PUBLIC
            "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
            "${CMAKE_SOURCE_DIR}/sdk/include")
    target_compile_definitions(vip_bindings_lr_vip_multi PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
    set(LR_VIP_MULTI_VIP_BINDING_TARGET vip_bindings_lr_vip_multi)
else()
    message(FATAL_ERROR "lr_vip_multi needs the generated vip consumer bindings (plugins/vip/include/external/plugify/include/vip/vip.cpp)")
endif()

# --- plugin -------------------------------------------------------------------
add_library(plugify-lr_vip_multi SHARED
        src/plugin.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:${LR_VIP_MULTI_LR_BINDING_TARGET}>
        $<TARGET_OBJECTS:${LR_VIP_MULTI_VIP_BINDING_TARGET}>
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
        plugify_sdk
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
# Engine-free tests of the multiplier and of the shipped groups.ini (build/tests so ../test.sh
# runs them). Nothing here links pisexcompat, plugify_sdk or an engine binding.
add_executable(lr_vip_multi_tests tests/lr_vip_multi_tests.cpp)
target_link_libraries(lr_vip_multi_tests PRIVATE lr_vip_multi_core -static-libstdc++ -static-libgcc)
target_compile_options(lr_vip_multi_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(lr_vip_multi_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        LR_VIP_MULTI_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
set_target_properties(lr_vip_multi_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

# lr_vip_multi_plugin_tests: the raw-game-event path of the plugin itself (the
# lr_weapons_plugin_tests / lr_namereward_plugin_tests pattern). tests/lr_vip_multi_plugin_tests.cpp
# includes ../src/plugin.cpp, so the real LR kill hook is driven with a fake raw s2sdk event: the
# "attacker" slot must come from GetEventPlayerSlot (the field is a player_controller_and_pawn one;
# GetEventInt is a different engine call that answers the raw engine user id, not a slot), -1/world
# drops the kill, a missing player-slot binding fails soft, the map gate closes in the OnMapEnd
# window and OnPluginEnd removes every registration. The __s2sdk_* / __levels_ranks_* / __vip_*
# binding globals are the compiled-in ones, filled with recording fakes. No pisexcompat: the target
# links plugify_sdk, the engine-free core and the three binding objects.
add_executable(lr_vip_multi_plugin_tests
        tests/lr_vip_multi_plugin_tests.cpp
        $<TARGET_OBJECTS:s2sdk_bindings>
        $<TARGET_OBJECTS:${LR_VIP_MULTI_LR_BINDING_TARGET}>
        $<TARGET_OBJECTS:${LR_VIP_MULTI_VIP_BINDING_TARGET}>
)
target_include_directories(lr_vip_multi_plugin_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
# SYSTEM: warnings from the generated lr/vip/plg/s2sdk headers, lr_api.hpp and vip_api.hpp must not
# fail -Werror on our sources (src/plugin.cpp is one of them).
target_include_directories(lr_vip_multi_plugin_tests SYSTEM PRIVATE
        "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include"
        "${CMAKE_SOURCE_DIR}/plugins/levels_ranks/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include"
        "${CMAKE_SOURCE_DIR}/plugins/vip/include/external/plugify/include"
        "${CMAKE_SOURCE_DIR}/sdk/include")
target_link_libraries(lr_vip_multi_plugin_tests PRIVATE
        plugify_sdk
        lr_vip_multi_core
        -static-libstdc++ -static-libgcc
)
target_compile_options(lr_vip_multi_plugin_tests PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(lr_vip_multi_plugin_tests PRIVATE
        _GLIBCXX_USE_CXX11_ABI=1
        PLUGIFY_MODULE="lr_vip_multi"
        PLUGIFY_IS_DEBUG=0
        PLUGIFY_IS_RELEASE=1)
set_target_properties(lr_vip_multi_plugin_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
