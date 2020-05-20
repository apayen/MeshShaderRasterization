#pragma once

#include "InstanceDeviceAndSwapchain.h"

class ParameterizedMesh
{
public:
	auto Initialize(InstanceDeviceAndSwapchain& device) -> bool;
	auto Uninitialize() -> bool;

	auto GetDescriptorSet() const -> VkDescriptorSet const& { return m_meshResources; }

private:
	VkImage m_positionTexture;	VmaAllocation m_positionTextureAllocation;
	VkImage m_albedoTexture;	VmaAllocation m_albedoTextureAllocation;
	VkImage m_normalTexture;	VmaAllocation m_normalTextureAllocation;

	VkImageView m_imageViews[3];

	VkDescriptorSet m_meshResources;
};