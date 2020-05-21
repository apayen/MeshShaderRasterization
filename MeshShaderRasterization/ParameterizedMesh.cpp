
#include "ParameterizedMesh.h"
#include <cmath>
#include <algorithm>

auto ParameterizedMesh::Initialize(InstanceDeviceAndSwapchain& device) -> bool
{
	VkResult result;

	VkDevice vkDevice = device.GetDevice();
	VmaAllocator allocator = device.GetAllocator();

	const uint32_t size = 8192;

	{
		VmaAllocationCreateInfo allocationCreateInfo;
		allocationCreateInfo.flags = 0;
		allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		allocationCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		allocationCreateInfo.preferredFlags = 0;
		allocationCreateInfo.memoryTypeBits = 0;
		allocationCreateInfo.pool = VK_NULL_HANDLE;
		allocationCreateInfo.pUserData = nullptr;

		VkImageCreateInfo imageCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
		imageCreateInfo.flags = 0;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		imageCreateInfo.extent.width = size;
		imageCreateInfo.extent.height = size;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.queueFamilyIndexCount = 0;
		imageCreateInfo.pQueueFamilyIndices = nullptr;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		result = vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &m_positionTexture, &m_positionTextureAllocation, nullptr);

		imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		result = vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &m_albedoTexture, &m_albedoTextureAllocation, nullptr);
		result = vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &m_normalTexture, &m_normalTextureAllocation, nullptr);
	}

	{
		VkBuffer stagingBuffer; VmaAllocation stagingBufferAllocation; VmaAllocationInfo allocationInfo;

		VmaAllocationCreateInfo allocationCreateInfo;
		allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		allocationCreateInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
		allocationCreateInfo.requiredFlags = 0;
		allocationCreateInfo.preferredFlags = 0;
		allocationCreateInfo.memoryTypeBits = 0;
		allocationCreateInfo.pool = VK_NULL_HANDLE;
		allocationCreateInfo.pUserData = nullptr;

		VkBufferCreateInfo bufferCreateInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
		bufferCreateInfo.flags = 0;
		bufferCreateInfo.size = size * size * sizeof(uint32_t) * 3;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.queueFamilyIndexCount = 0;
		bufferCreateInfo.pQueueFamilyIndices = nullptr;
		result = vmaCreateBuffer(allocator, &bufferCreateInfo, &allocationCreateInfo, &stagingBuffer, &stagingBufferAllocation, &allocationInfo);

		void* data;
		vkMapMemory(vkDevice, allocationInfo.deviceMemory, allocationInfo.offset, allocationInfo.size, 0, &data);

		VkMappedMemoryRange memoryRange{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr };
		memoryRange.memory = allocationInfo.deviceMemory;
		memoryRange.offset = allocationInfo.offset;
		memoryRange.size = allocationInfo.size;
		vkFlushMappedMemoryRanges(vkDevice, 1, &memoryRange);
		vkUnmapMemory(vkDevice, allocationInfo.deviceMemory);

		const float pi = 3.14159265359f;

		uint32_t* position = (uint32_t*)(data);
		uint32_t* albedo = position + size * size;
		uint32_t* normal = albedo + size * size;

		for (uint32_t i = 0; i < size; ++i)
		{
			float y = std::sin(i / float(size - 1) * 2 * pi) / 2.0f + 0.5f;
			float r = std::cos(i / float(size - 1) * 2 * pi);
			for (uint32_t j = 0; j < size; ++j)
			{
				float z = r * std::sin(j / float(size - 1) * 2 * pi) / 2.0f + 0.5f;
				float x = r * std::cos(j / float(size - 1) * 2 * pi) / 2.0f + 0.5f;
				*(position++) = (uint32_t(std::clamp(x, 0.0f, 1.0f) * 1023.0f) << 0)
					          | (uint32_t(std::clamp(y, 0.0f, 1.0f) * 1023.0f) << 10)
					          | (uint32_t(std::clamp(z, 0.0f, 1.0f) * 1023.0f) << 20)
					          ;

				*(albedo++) = 0xff000000
						    | ((j & 0xff) << 8)
						    | ((i & 0xff) << 0)
						    ;

				*(normal++) = (uint32_t(std::clamp(x, 0.0f, 1.0f) * 255.0f) << 0)
					        | (uint32_t(std::clamp(y, 0.0f, 1.0f) * 255.0f) << 8)
					        | (uint32_t(std::clamp(z, 0.0f, 1.0f) * 255.0f) << 16)
					        ;
			}
		}

		device.BeginFrame();
		VkCommandBuffer commandBuffer = device.GetCommandBuffer();

		VkImageMemoryBarrier imageMemoryBarrier[3];
		for (uint32_t i = 0; i < std::size(imageMemoryBarrier); ++i)
		{
			imageMemoryBarrier[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
			imageMemoryBarrier[i].srcAccessMask = 0;
			imageMemoryBarrier[i].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageMemoryBarrier[i].subresourceRange.baseMipLevel = 0;
			imageMemoryBarrier[i].subresourceRange.levelCount = 1;
			imageMemoryBarrier[i].subresourceRange.baseArrayLayer = 0;
			imageMemoryBarrier[i].subresourceRange.layerCount = 1;
		}
		imageMemoryBarrier[0].image = m_positionTexture;
		imageMemoryBarrier[1].image = m_albedoTexture;
		imageMemoryBarrier[2].image = m_normalTexture;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, uint32_t(std::size(imageMemoryBarrier)), imageMemoryBarrier);

		VkBufferImageCopy region;
		region.bufferOffset = allocationInfo.offset;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset.x = 0;
		region.imageOffset.y = 0;
		region.imageOffset.z = 0;
		region.imageExtent.width = size;
		region.imageExtent.height = size;
		region.imageExtent.depth = 1;
		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_positionTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		region.bufferOffset += size * size * sizeof(uint32_t);
		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_albedoTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		region.bufferOffset += size * size * sizeof(uint32_t);
		vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_normalTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

		for (uint32_t i = 0; i < std::size(imageMemoryBarrier); ++i)
		{
			imageMemoryBarrier[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			imageMemoryBarrier[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV, 0, 0, nullptr, 0, nullptr, uint32_t(std::size(imageMemoryBarrier)), imageMemoryBarrier);

		device.EndFrame();
		device.WaitIdle();

		vmaDestroyBuffer(allocator, stagingBuffer, stagingBufferAllocation);
	}

	{
		VkImageViewCreateInfo imageViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
		imageViewCreateInfo.flags = 0;
		imageViewCreateInfo.image = m_positionTexture;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = 1;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_imageViews[0]);

		imageViewCreateInfo.image = m_albedoTexture;
		imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_imageViews[1]);

		imageViewCreateInfo.image = m_normalTexture;
		vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_imageViews[2]);
	}

	{
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
		descriptorSetAllocateInfo.descriptorPool = device.GetDescriptorPool();
		descriptorSetAllocateInfo.descriptorSetCount = 1;
		descriptorSetAllocateInfo.pSetLayouts = &device.GetParameterizedMeshDescriptorSetLayout();
		vkAllocateDescriptorSets(vkDevice, &descriptorSetAllocateInfo, &m_meshResources);

		VkDescriptorImageInfo imageInfos[3];
		VkWriteDescriptorSet writeDescriptorSets[3];
		for (uint32_t i = 0; i < std::size(writeDescriptorSets); ++i)
		{
			imageInfos[i].sampler = VK_NULL_HANDLE;
			imageInfos[i].imageView = m_imageViews[i];
			imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			writeDescriptorSets[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
			writeDescriptorSets[i].dstSet = m_meshResources;
			writeDescriptorSets[i].dstBinding = i;
			writeDescriptorSets[i].dstArrayElement = 0;
			writeDescriptorSets[i].descriptorCount = 1;
			writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[i].pImageInfo = &imageInfos[i];
			writeDescriptorSets[i].pBufferInfo = nullptr;
			writeDescriptorSets[i].pTexelBufferView = nullptr;
		}
		vkUpdateDescriptorSets(vkDevice, uint32_t(std::size(writeDescriptorSets)), writeDescriptorSets, 0, nullptr);
	}

	return true;
}

auto ParameterizedMesh::Uninitialize() -> bool
{
	return true;
}
