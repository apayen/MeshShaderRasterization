#include "ShaderModule.h"

#include "shaderc/shaderc.h"
#include <string>
#include <fstream>

ShaderModule::ShaderModule()
	: m_shaderModule(VK_NULL_HANDLE)
{
}

auto ShaderModule::Initialize(VkDevice device, std::string const& filepath, VkShaderStageFlagBits stage, std::vector<char const*> const& defines) -> bool
{
	VkResult result;

	shaderc_shader_kind kind;
	switch (stage)
	{
	case VK_SHADER_STAGE_FRAGMENT_BIT: kind = shaderc_fragment_shader;  break;
	case VK_SHADER_STAGE_COMPUTE_BIT: kind = shaderc_compute_shader;  break;
	case VK_SHADER_STAGE_TASK_BIT_NV: kind = shaderc_task_shader;  break;
	case VK_SHADER_STAGE_MESH_BIT_NV: kind = shaderc_mesh_shader;  break;
	default:
		std::cerr << "Unsupported shader stage";
		return false;
	}

	std::ifstream file(filepath, std::ios::ate | std::ios::binary | std::ios::in);
	
	if (!file.good())
	{
		std::cerr << "could not read " << filepath << std::endl;
		return false;
	}

	std::vector<char> shaderSource;
	shaderSource.resize(file.tellg());
	file.seekg(0);
	file.read(shaderSource.data(), shaderSource.size());
	file.close();

	shaderc_compiler_t compiler = shaderc_compiler_initialize();
	shaderc_compile_options_t options = shaderc_compile_options_initialize();
	shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
	shaderc_compile_options_set_generate_debug_info(options);
	shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);
	shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_1);
	for (char const* define : defines)
	{
		char const* end = define + strlen(define);
		char const* sep = std::find(define, end, '=');
		if (sep != end)
			shaderc_compile_options_add_macro_definition(options, define, sep - define, sep + 1, end - sep - 1);
		else
			shaderc_compile_options_add_macro_definition(options, define, end - define, nullptr, 0);
	}

	shaderc_compilation_result_t shaderResult = shaderc_compile_into_spv(compiler, shaderSource.data(), shaderSource.size(), kind, filepath.c_str(), "main", options);
	shaderc_compile_options_release(options);

	if (shaderc_result_get_compilation_status(shaderResult) == shaderc_compilation_status_success)
	{
		VkShaderModuleCreateInfo shaderModuleCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr };
		shaderModuleCreateInfo.flags = 0;
		shaderModuleCreateInfo.codeSize = shaderc_result_get_length(shaderResult);
		shaderModuleCreateInfo.pCode = (uint32_t*)shaderc_result_get_bytes(shaderResult);
		result = vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &m_shaderModule);
		if (result != VK_SUCCESS)
			std::cerr << "failed creating shader module" << std::endl;
	}
	else
	{
		std::cerr << shaderc_result_get_error_message(shaderResult) << std::endl;
	}

	shaderc_result_release(shaderResult);
	shaderc_compiler_release(compiler);

	return m_shaderModule != VK_NULL_HANDLE;
}

auto ShaderModule::Unitialize(VkDevice device) -> void
{
	vkDestroyShaderModule(device, m_shaderModule, nullptr);
	m_shaderModule = VK_NULL_HANDLE;
}