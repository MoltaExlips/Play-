cmake_minimum_required(VERSION 3.15)

# macOS deployment target needs to be set before 'project' to work
if(APPLE AND NOT TARGET_IOS)
	set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
endif()

project(Play)

include(GNUInstallDirs)

set(CMAKE_MODULE_PATH
	${CMAKE_CURRENT_SOURCE_DIR}/deps/Dependencies/cmake-modules
	${CMAKE_MODULE_PATH}
)
include(Header)

if(EMSCRIPTEN)
	# Performance options for the WebAssembly build.
	option(PLAYJS_WASM_SIMD "Compile with WebAssembly SIMD (-msimd128)" ON)
	option(PLAYJS_WASM_EXCEPTIONS "Use native WebAssembly exceptions instead of JS-emulated ones" ON)
	option(PLAYJS_LTO "Use link-time optimization for Release builds" ON)
	#Chains compiled EE/IOP blocks with wasm tail calls instead of returning to the executor
	#loop after each block. Needs browsers with tail-call support (Chrome 112, Firefox 121,
	#Safari 18.2).
	option(PLAYJS_TAIL_CALLS "Link compiled blocks with wasm tail calls" OFF)
	if(PLAYJS_TAIL_CALLS)
		add_compile_definitions(PLAYJS_TAIL_CALLS=1 CODEGEN_WASM_TAIL_CALLS=1)
		add_compile_options(-mtail-call)
		add_link_options(-mtail-call)
	endif()
	if(PLAYJS_WASM_SIMD)
		# Turns Emscripten's NEON headers (used through FRAMEWORK_SIMD_USE_NEON) into real
		# WebAssembly SIMD instead of scalar fallbacks, and lets the compiler auto-vectorize.
		add_compile_options(-msimd128)
		add_link_options(-msimd128)
	endif()
	if(PLAYJS_WASM_EXCEPTIONS)
		# JS-emulated exceptions route calls that may throw through JavaScript invoke_* wrappers.
		string(REPLACE "-fexceptions" "-fwasm-exceptions" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
		add_link_options(-fwasm-exceptions)
	endif()
	if(PLAYJS_LTO)
		add_compile_options($<$<CONFIG:Release>:-flto>)
		add_link_options($<$<CONFIG:Release>:-flto>)
	endif()
endif()

set(BUILD_PLAY ON CACHE BOOL "Build Play! Emulator")
set(BUILD_PSFPLAYER OFF CACHE BOOL "Build PsfPlayer")
set(BUILD_TESTS ON CACHE BOOL "Build Tests")
set(USE_AOT_CACHE OFF CACHE BOOL "Use AOT block cache")
set(BUILD_AOT_CACHE OFF CACHE BOOL "Build AOT block cache (for PsfPlayer only)")
set(BUILD_LIBRETRO_CORE OFF CACHE BOOL "Build Libretro Core")

set(PROJECT_NAME "Play!")
set(PROJECT_Version 0.30)

if(BUILD_TESTS)
	enable_testing()
endif()

find_package(Git)
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git/")
	execute_process(COMMAND ${GIT_EXECUTABLE} describe --always
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		RESULT_VARIABLE GIT_DESCRIBE_RESULT
		OUTPUT_VARIABLE GIT_TAG
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
	if(GIT_DESCRIBE_RESULT EQUAL 0)
		message("Building for ${GIT_TAG}.")
		set(PROJECT_Version ${GIT_TAG})
	else()
		message("Warning: Failed to obtain Git tag.")
	endif()
else()
	message("Warning: Not a git repo or git not found.")
endif()

add_definitions(-DPLAY_VERSION="${PROJECT_Version}")
set(PROJECT_LIBS)

include(PrecompiledHeader)

if(BUILD_LIBRETRO_CORE)
	if(NOT MSVC)
		add_compile_options($<$<NOT:$<COMPILE_LANGUAGE:Swift>>:-fPIC>)
	endif()
	add_subdirectory(Source/ui_libretro)
endif()

if(BUILD_PLAY)
	if(TARGET_PLATFORM_UNIX OR TARGET_PLATFORM_MACOS OR TARGET_PLATFORM_WIN32 OR USE_QT)
		add_subdirectory(Source/ui_qt/)
		add_subdirectory(tools/ElfView)
		add_subdirectory(tools/DiscChecker)
	endif(TARGET_PLATFORM_UNIX OR TARGET_PLATFORM_MACOS OR TARGET_PLATFORM_WIN32 OR USE_QT)

	if(NOT USE_QT)
		if(TARGET_PLATFORM_IOS)
			add_subdirectory(Source/ui_ios/)
		endif(TARGET_PLATFORM_IOS)

		if(TARGET_PLATFORM_ANDROID)
			add_subdirectory(Source/ui_android/)
		endif(TARGET_PLATFORM_ANDROID)

		if(TARGET_PLATFORM_JS)
			add_subdirectory(Source/ui_js)
		endif(TARGET_PLATFORM_JS)
	endif()
endif(BUILD_PLAY)

if(BUILD_TESTS)
	add_subdirectory(tools/AutoTest/)
	add_subdirectory(tools/GsAreaTest/)
	add_subdirectory(tools/McServTest/)
	add_subdirectory(tools/SpuTest/)
	add_subdirectory(tools/VuTest/)
	add_subdirectory(deps/Framework/build_cmake/Tests)
endif()

add_subdirectory(tools/NamcoSys147NANDTools)

if(BUILD_PSFPLAYER)
	add_subdirectory(tools/PsfPlayer)
endif(BUILD_PSFPLAYER)
