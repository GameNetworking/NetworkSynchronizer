cmake_minimum_required(VERSION 3.13)
project(NetworkSynchronizer CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# When turning this option on, the library will be compiled with debug symbols
option(GENERATE_DEBUG_SYMBOLS "Generate debug symbols" OFF)

# When turning this option on, the library will be compiled with extra checks and assertions.
option(ENABLE_DEBUG "Enable debug checks" OFF)

# When turning this option on, the library will be compiled with extra checks and assertions.
option(ENABLE_DEBUG_DATA_BUFFER "Enable debug data buffer." OFF)

# When turning this option on, you can override the release and debug CPP build flags.
option(OVERRIDE_CXX_FLAGS "Enable this to override the flags used for release and debug. `CMAKE_CXX_FLAGS_DEBUG` `CMAKE_CXX_FLAGS_RELEASE`" OFF)

# Ability to toggle between the static and DLL versions of the MSVC runtime library
# Windows Store only supports the DLL version
option(USE_STATIC_MSVC_RUNTIME_LIBRARY "Use the static MSVC runtime library" ON)

# Ability to toggle between the static and DLL versions of the MSVC runtime library
# Windows Store only supports the DLL version
option(BUILD_MSVC_MD "In MSVC build the library using MD." ON)

# Enables the debugger UI. Allows to debug the data sent and received by any peers to identify bugs.
option(ENABLE_DEBUGGER_UI "Enables the debugger UI." OFF)

if(ENABLE_DEBUGGER_UI)
    # Find Python interpreter used to load the debugger_ui into C++.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Command to execute the Python script
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/../core/__generated__debugger_ui.h
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../debugger_ui/cpplize_debugger.py ${CMAKE_CURRENT_SOURCE_DIR}/..
        COMMENT "Running Python script to generate the __generated__debugger_ui.h file"
    )
    
    # Include the generated file as part of the build dependencies
    add_custom_target(RunPythonScript ALL
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../core/__generated__debugger_ui.h
    )
endif()

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:precise") # Ensure floating point is not fast to make math operations deterministic.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /fp:precise") # Ensure floating point is not fast to make math operations deterministic.
    
    # Disable RTTI
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
    
	# Set runtime library
	if (USE_STATIC_MSVC_RUNTIME_LIBRARY)
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	endif()
	
	# Optionally generate debug symbols
	if (GENERATE_DEBUG_SYMBOLS)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi")
	endif()
	
	# Set linker flags
	if (GENERATE_DEBUG_SYMBOLS)
    	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
    endif()
    
    if (BUILD_MSVC_MD)
	    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MD")
	    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /MD")
	endif()
	
    # Set compiler flags for various configurations
    if (NOT OVERRIDE_CXX_FLAGS)
    	set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /GS /Od /Ob0 /RTC1")
        if (BUILD_MSVC_MD)
    	    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd")
	        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MDd")
        endif()
    	set(CMAKE_CXX_FLAGS_RELEASE "/GS- /Gy /O2 /Oi /Ot")
    endif()
    set(CMAKE_CXX_FLAGS_DISTRIBUTION "${CMAKE_CXX_FLAGS_RELEASE}")
else()
    # Floating point is always precise to ensure determinism across platforms.
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-fast-math -frounding-math -fno-trapping-math")
    set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-fast-math -frounding-math -fno-trapping-math")
    
    # Disable RTTI
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
    
	if (GENERATE_DEBUG_SYMBOLS)
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
	endif()
	
	if (NOT OVERRIDE_CXX_FLAGS)
		set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
		set(CMAKE_CXX_FLAGS_RELEASE "-O3")
	endif()
	set(CMAKE_CXX_FLAGS_DISTRIBUTION "${CMAKE_CXX_FLAGS_RELEASE}")
endif()

# Set repository root
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../)

# Network Synchronizer
set(NS_ROOT ${REPO_ROOT}/NetworkSynchronizer)
set(NS_SRC_FILES
	${NS_ROOT}/scene_synchronizer.h
	${NS_ROOT}/scene_synchronizer.cpp
	${NS_ROOT}/core/bit_array.h
	${NS_ROOT}/core/bit_array.cpp
	${NS_ROOT}/core/bitcasts.h
	${NS_ROOT}/core/core.h
	${NS_ROOT}/core/core.cpp
	${NS_ROOT}/core/scheduled_procedure.h
	${NS_ROOT}/core/data_buffer.h
	${NS_ROOT}/core/data_buffer.cpp
	${NS_ROOT}/core/ensure.h
	${NS_ROOT}/core/ensure.cpp
	${NS_ROOT}/core/fp16.h
	${NS_ROOT}/core/json.hpp
	${NS_ROOT}/core/net_math.h
	${NS_ROOT}/core/net_utilities.h
	${NS_ROOT}/core/net_utilities.cpp
	${NS_ROOT}/core/network_codec.h
	${NS_ROOT}/core/network_codec.cpp
	${NS_ROOT}/core/network_interface_define.h
	${NS_ROOT}/core/network_interface.h
	${NS_ROOT}/core/network_interface.cpp
	${NS_ROOT}/core/object_data.h
	${NS_ROOT}/core/object_data.cpp
	${NS_ROOT}/core/object_data_storage.h
	${NS_ROOT}/core/object_data_storage.cpp
	${NS_ROOT}/core/peer_data.h
	${NS_ROOT}/core/peer_data.cpp
	${NS_ROOT}/core/peer_networked_controller.h
	${NS_ROOT}/core/peer_networked_controller.cpp
	${NS_ROOT}/core/processor.h
	${NS_ROOT}/core/scene_synchronizer_debugger.h
	${NS_ROOT}/core/scene_synchronizer_debugger.cpp
	${NS_ROOT}/core/scene_synchronizer_debugger_json_storage.h
	${NS_ROOT}/core/snapshot.h
	${NS_ROOT}/core/snapshot.cpp
	${NS_ROOT}/core/var_data.h
	${NS_ROOT}/core/var_data.cpp
	
	# TODO disable tests in shipping builds.
	# Test
	${NS_ROOT}/tests/local_network.cpp
	${NS_ROOT}/tests/local_network.h
	${NS_ROOT}/tests/local_scene.cpp
	${NS_ROOT}/tests/local_scene.h
	${NS_ROOT}/tests/test_data_buffer.cpp
	${NS_ROOT}/tests/test_data_buffer.h
	${NS_ROOT}/tests/test_doll_simulation.cpp
	${NS_ROOT}/tests/test_doll_simulation.h
	${NS_ROOT}/tests/test_AI_simulation.cpp
	${NS_ROOT}/tests/test_AI_simulation.h
	${NS_ROOT}/tests/test_math_lib.cpp
	${NS_ROOT}/tests/test_math_lib.h
	${NS_ROOT}/tests/test_processor.cpp
	${NS_ROOT}/tests/test_processor.h
	${NS_ROOT}/tests/test_scene_synchronizer.cpp
	${NS_ROOT}/tests/test_scene_synchronizer.h
	${NS_ROOT}/tests/test_simulation.cpp
	${NS_ROOT}/tests/test_simulation.h
	${NS_ROOT}/tests/test_switch_controller.cpp
	${NS_ROOT}/tests/test_switch_controller.h
	${NS_ROOT}/tests/tests.cpp
	${NS_ROOT}/tests/tests.h
)

if(ENABLE_DEBUGGER_UI)
    add_compile_definitions(UI_DEBUGGER_ENABLED)
    list(APPEND NS_SRC_FILES ${NS_ROOT}/core/__generated__debugger_ui.h)
endif()
	
# Group source files
source_group(TREE ${NS_ROOT} FILES ${NS_SRC_FILES})

# Create NetworkSynchronizer lib
add_library(NetworkSynchronizer ${NS_SRC_FILES})

target_include_directories(NetworkSynchronizer PUBLIC ${REPO_ROOT})

# Setting enable asserts flag
if (ENABLE_DEBUG)
	target_compile_definitions(NetworkSynchronizer PUBLIC NS_DEBUG_ENABLED)
endif()

if (ENABLE_DEBUG_DATA_BUFFER)
	target_compile_definitions(NetworkSynchronizer PUBLIC DEBUG_DATA_BUFFER)
endif()

if(ENABLE_DEBUGGER_UI)
    add_dependencies(NetworkSynchronizer RunPythonScript)
endif()
