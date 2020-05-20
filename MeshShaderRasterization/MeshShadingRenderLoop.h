#pragma once

#include "InstanceDeviceAndSwapchain.h"
#include "ShaderModule.h"
#include "ParameterizedMesh.h"

class MeshShadingRenderLoop
{
public:
	auto Initialize(InstanceDeviceAndSwapchain const& device) -> bool;
	auto Uninitialize() -> void;

	auto RenderLoop(InstanceDeviceAndSwapchain& device) -> bool;

	auto AddMeshInstance(ParameterizedMesh const* meshInstance) -> void;

private:
	const uint32_t maxWidth = 3840;
	const uint32_t maxHeight = 2160;

	VkBuffer m_viewportConstantsBuffer; VmaAllocation m_viewportConstantsAllocation;

	VkImage m_depthBuffer;  VmaAllocation m_depthAllocation;
	VkImage m_depthStorageBuffer;  VmaAllocation m_depthStorageAllocation;
	VkImage m_albedoBuffer; VmaAllocation m_albedoAllocation;
	VkImage m_normalBuffer; VmaAllocation m_normalAllocation;

	VkImageView m_framebufferViews[3];
	VkImageView m_meshShaderViews[3];
	VkImageView m_combineAndLightViews[2];

	VkFramebuffer m_framebuffer;

	VkRenderPass m_renderPass;

	ShaderModule m_taskShader;
	ShaderModule m_depthPassMeshShader;
	ShaderModule m_gbufferPassMeshShader;
	ShaderModule m_gbufferPassFragmentShader;
	ShaderModule m_combineAndLightComputeShader;

	VkDescriptorSetLayout m_viewportResourcesLayout;
	VkPipelineLayout m_graphicPipelineLayout;
	VkDescriptorSet m_viewportResources;

	VkDescriptorSetLayout m_combineAndLightResourcesLayout;
	VkDescriptorSetLayout m_swapchainResourcesLayout;
	VkPipelineLayout m_combineAndLightPipelineLayout;
	VkDescriptorSet m_combineAndLightResources;

	VkPipeline m_meshDepthPass;
	VkPipeline m_meshGbufferPass;
	VkPipeline m_combineAndLight;

	std::vector<ParameterizedMesh const*> m_meshInstances;
};
