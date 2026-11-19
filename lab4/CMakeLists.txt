cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0135)
	cmake_policy(SET CMP0135 NEW)
endif()
include(FetchContent)

project(lab4-shadow-mapping LANGUAGES C CXX)

add_executable(${PROJECT_NAME} source/main.cpp source/graphics_internal.cpp source/application.cpp)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(APPLE)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(WIN32)
	if(MSVC)
		target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4201)
	else()
		target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
		# Keep a console on MinGW so Vulkan validation and startup errors stay visible.
	endif()
	target_compile_definitions(${PROJECT_NAME} PRIVATE -D_USE_MATH_DEFINES)
else()
	message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is not supported")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)

FetchContent_Declare(
	glfw
	URL https://codeload.github.com/glfw/glfw/tar.gz/refs/tags/3.5.1
	URL_HASH SHA256=5234f4f29473e9a06bc7847d8371858dd135d38466eeeaa652fdc9f8f9ff0c20
)

FetchContent_Declare(
	vk-bootstrap
	URL https://codeload.github.com/charles-lunarg/vk-bootstrap/tar.gz/refs/tags/v1.4.321
	URL_HASH SHA256=4d4d03523822dd684239cce45a48a14f1c4f653c870e0982ebf09d9cea3299e4
)

FetchContent_Declare(
	VulkanMemoryAllocator
	URL https://codeload.github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/tar.gz/refs/tags/v3.4.0
	URL_HASH SHA256=822aa850c6ce77346ae96a8a1d351d52e77e85929f35363849a0a4e638e0a2a1
)

FetchContent_Declare(
	imgui
	URL https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v1.92.9
	URL_HASH SHA256=af97ed649182c39314320514a672b82008ab462b9293fe23d37b30bfa5d05519
)

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(GLFW_BUILD_WAYLAND OFF)
endif()

FetchContent_MakeAvailable(
	glfw
	vk-bootstrap
	VulkanMemoryAllocator
	imgui
)

target_link_libraries(${PROJECT_NAME} PRIVATE
	glfw
	Vulkan::Vulkan
	vk-bootstrap::vk-bootstrap
	GPUOpen::VulkanMemoryAllocator
)

target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_sources(${PROJECT_NAME} PRIVATE
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/imgui_demo.cpp
)

# Compile shaders
find_program(GLSLC_FOUND NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin")
find_program(GLSLANG_FOUND NAMES glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin")
if(GLSLC_FOUND)
	set(SHADER_COMPILER "${GLSLC_FOUND}")
	set(SHADER_FLAGS --target-env=vulkan1.3)
elseif(GLSLANG_FOUND)
	set(SHADER_COMPILER "${GLSLANG_FOUND}")
	set(SHADER_FLAGS -V --target-env vulkan1.3)
else()
	message(FATAL_ERROR "Install glslc or glslangValidator to compile shaders.")
endif()

if(SHADER_COMPILER)
	set(_SHADER_BINARIES)

	macro(compile_shader SHADER_FILE)
		set(SHADER_SOURCE ${CMAKE_SOURCE_DIR}/shaders/${SHADER_FILE})
		set(SHADER_BINARY ${SHADER_FILE}.spv)
		set(SHADER_BINARY_PATH ${CMAKE_CURRENT_BINARY_DIR}/compiled-shaders/${SHADER_BINARY})

		add_custom_command(
			OUTPUT ${SHADER_BINARY_PATH}
			COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/compiled-shaders"
			COMMAND "${SHADER_COMPILER}" ${SHADER_FLAGS} "${SHADER_SOURCE}" -o "${SHADER_BINARY_PATH}"
			DEPENDS ${SHADER_SOURCE}
			COMMENT "Compiling ${SHADER_FILE} shader"
			VERBATIM
		)

		list(APPEND _SHADER_BINARIES ${SHADER_BINARY_PATH})
	endmacro()

	# To compile shader file, use compile_shader function with a file name
	# of a shader inside shaders directory. See example below:

	compile_shader(lit.vert)
	compile_shader(lit.frag)
	compile_shader(unlit.vert)
	compile_shader(unlit.frag)
	compile_shader(shadow.vert)

	add_custom_target(shaders DEPENDS ${_SHADER_BINARIES})
	add_dependencies(${PROJECT_NAME} shaders)
	add_custom_target(runtime_assets ALL
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_BINARY_DIR}/compiled-shaders" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CMAKE_CURRENT_SOURCE_DIR}/textures" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/textures"
		DEPENDS ${_SHADER_BINARIES}
		VERBATIM)
	add_dependencies(runtime_assets shaders)
	add_dependencies(${PROJECT_NAME} runtime_assets)
endif()
