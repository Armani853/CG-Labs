#include <cstdint>
#include <climits>

#include <iostream>
#include <filesystem>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include "graphics_internal.hpp"
#include "application.hpp"

namespace {

constexpr int32_t default_window_width = 1280;
constexpr int32_t default_window_height = 720;

constexpr char default_window_title[] = "Computer Graphics Lab 4 - Shadow Mapping";

GLFWwindow* glfw_window;

} // namespace

int main(int argc, char** argv) {
	std::error_code filesystem_error;
	const auto executable = argc > 0 ? std::filesystem::absolute(argv[0], filesystem_error)
	                                 : std::filesystem::path{};
	if (!filesystem_error && !executable.empty())
		std::filesystem::current_path(executable.parent_path(), filesystem_error);
	if (filesystem_error) {
		std::cerr << "Cannot locate the runtime assets: " << filesystem_error.message() << '\n';
		return EXIT_FAILURE;
	}
	int status = EXIT_SUCCESS;

	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW\n";
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	glfw_window = glfwCreateWindow(default_window_width, default_window_height,
	                               default_window_title, nullptr, nullptr);
	if (glfw_window == nullptr) {
		status = EXIT_FAILURE;
		goto err_null_window;
	}

	glfwSetFramebufferSizeCallback(glfw_window, [](GLFWwindow*, int width, int height){
		if (width == 0 || height == 0) {
			return;
		}

		graphics::internal::resize(width, height);
	});

	if (ImGui::CreateContext() == nullptr) {
		std::cerr << "Failed to create ImGUI context\n";
		status = EXIT_FAILURE;
		goto err_imgui_init;
	}
	{
		ImGui::GetIO().IniFilename = nullptr;
		ImFontConfig font_config;
		font_config.SizePixels = 18.0f;
		ImGui::GetIO().Fonts->AddFontDefault(&font_config);
		ImGui::GetStyle().ScaleAllSizes(1.2f);
	}

	if (!ImGui_ImplGlfw_InitForVulkan(glfw_window, true)) {
		std::cerr << "Failed to initialize ImGUI GLFW backend for Vulkan renderer\n";
		status = EXIT_FAILURE;
		goto err_imgui_glfw_init;
	}

	if (!graphics::internal::initialize(glfw_window)) {
		std::cerr << "Failed to initialize graphics\n";
		status = EXIT_FAILURE;
		goto err_graphics_init;
	}

	if (!application::initialize()) {
		std::cerr << "Failed to initialize application\n";
		status = EXIT_FAILURE;
		application::shutdown();
		goto err_application_init;
	}

	while (!glfwWindowShouldClose(glfw_window)) {
		glfwPollEvents();
		int width, height;
		glfwGetFramebufferSize(glfw_window, &width, &height);
		if (width == 0 || height == 0) {
			glfwWaitEvents();
			continue;
		}
		const double time = glfwGetTime();
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();

		ImGui::NewFrame();
		application::update(time);
		ImGui::Render();

		graphics::internal::FrameData fd = graphics::internal::prepare();
		if (fd.command_buffer == VK_NULL_HANDLE) {
			std::cerr << "Failed to prepare Vulkan frame\n";
			status = EXIT_FAILURE;
			break;
		}
		application::render(fd);
		graphics::internal::submitAndPresent();
	}

	application::shutdown();
err_application_init:
err_graphics_init:
	graphics::internal::shutdown();
	ImGui_ImplGlfw_Shutdown();
err_imgui_glfw_init:
	ImGui::DestroyContext();
err_imgui_init:
	glfwDestroyWindow(glfw_window);
err_null_window:
	glfwTerminate();

	return status;
}
