#pragma once

#include "InstanceDeviceAndSwapchain.h"

class ShaderModule
{
public:
	ShaderModule();

	auto Initialize(VkDevice device, std::string const& filepath, VkShaderStageFlagBits stage, std::vector<char const*> const& defines) -> bool;
	auto Unitialize(VkDevice device) -> void;

	auto GetShaderModule() const -> VkShaderModule { return m_shaderModule; }

private:
	VkShaderModule m_shaderModule;
};