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

if (MSVC)
	# Optionally generate debug symbols
	if (GENERATE_DEBUG_SYMBOLS)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Zi")
	endif()
	
	# Set linker flags
	if (GENERATE_DEBUG_SYMBOLS)
    	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
    endif()
    
    # Set compiler flags for various configurations
    if (NOT OVERRIDE_CXX_FLAGS)
    	set(CMAKE_CXX_FLAGS_DEBUG "/GS /Od /Ob0 /RTC1")
    	set(CMAKE_CXX_FLAGS_RELEASE "/GS- /Gy /O2 /Oi /Ot")
    endif()
    set(CMAKE_CXX_FLAGS_DISTRIBUTION "${CMAKE_CXX_FLAGS_RELEASE}")
else()
	if (GENERATE_DEBUG_SYMBOLS)
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DEBUG")
	endif()
	
	if (NOT OVERRIDE_CXX_FLAGS)
		set(CMAKE_CXX_FLAGS_DEBUG "")
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
	${NS_ROOT}/core/network_interface.h
	${NS_ROOT}/core/object_data.h
	${NS_ROOT}/core/object_data.cpp
	${NS_ROOT}/core/object_data_storage.h
	${NS_ROOT}/core/object_data_storage.cpp
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
)
	
# Group source files
source_group(TREE ${NS_ROOT} FILES ${NS_SRC_FILES})

# Create NetworkSynchronizer lib
add_library(NetworkSynchronizer ${NS_SRC_FILES})

target_include_directories(NetworkSynchronizer PUBLIC ${REPO_ROOT})

# Setting enable asserts flag
if (ENABLE_DEBUG)
	target_compile_definitions(NetworkSynchronizer PUBLIC DEBUG_ENABLED)
endif()

if (ENABLE_DEBUG_DATA_BUFFER)
	target_compile_definitions(NetworkSynchronizer PUBLIC DEBUG_DATA_BUFFER)
endif()
