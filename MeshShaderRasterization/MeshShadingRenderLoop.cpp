#include "MeshShadingRenderLoop.h"

struct ViewportConstants
{
	float projectionMatrix[4][4];
	float viewportSize[4];
};

auto MeshShadingRenderLoop::Initialize(InstanceDeviceAndSwapchain const& device) -> bool
{
	VkResult result;

	VkDevice vkDevice = device.GetDevice();
	VmaAllocator allocator = device.GetAllocator();

	m_taskShader.Initialize(vkDevice, "shaders/test_ts.glsl", VK_SHADER_STAGE_TASK_BIT_NV, {});
	m_depthPassMeshShader.Initialize(vkDevice, "shaders/test_ms.glsl", VK_SHADER_STAGE_MESH_BIT_NV, {"DEPTH_PASS"});
	m_gbufferPassMeshShader.Initialize(vkDevice, "shaders/test_ms.glsl", VK_SHADER_STAGE_MESH_BIT_NV, {"GBUFFER_PASS"});
	m_gbufferPassFragmentShader.Initialize(vkDevice, "shaders/test_fs.glsl", VK_SHADER_STAGE_FRAGMENT_BIT, {});
	m_combineAndLightComputeShader.Initialize(vkDevice, "shaders/combine_and_light.glsl", VK_SHADER_STAGE_COMPUTE_BIT, {});

	{
		VmaAllocationCreateInfo allocationCreateInfo;
		allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
		allocationCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		allocationCreateInfo.preferredFlags = 0;
		allocationCreateInfo.memoryTypeBits = 0;
		allocationCreateInfo.pool = VK_NULL_HANDLE;
		allocationCreateInfo.pUserData = nullptr;

		VkBufferCreateInfo bufferCreateInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr };
		bufferCreateInfo.flags = 0;
		bufferCreateInfo.size = sizeof(ViewportConstants);
		bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		bufferCreateInfo.queueFamilyIndexCount = 0;
		bufferCreateInfo.pQueueFamilyIndices = nullptr;
		result = vmaCreateBuffer(allocator, &bufferCreateInfo, &allocationCreateInfo, &m_viewportConstantsBuffer, &m_viewportConstantsAllocation, nullptr);

		VkImageCreateInfo imageCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr };
		imageCreateInfo.flags = 0;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = VK_FORMAT_D32_SFLOAT;
		imageCreateInfo.extent.width = maxWidth;
		imageCreateInfo.extent.height = maxHeight;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.queueFamilyIndexCount = 0;
		imageCreateInfo.pQueueFamilyIndices = nullptr;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		result = vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &m_depthBuffer, &m_depthAllocation, nullptr);

		imageCreateInfo.format = VK_FORMAT_R32_UINT;
		imageCreateInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		result = vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &m_depthStorageBuffer, &m_depthStorageAllocation, nullptr);

		imageCreateInfo.arrayLayers = 2;
		imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		result = vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &m_albedoBuffer, &m_albedoAllocation, nullptr);

		result = vmaCreateImage(allocator, &imageCreateInfo, &allocationCreateInfo, &m_normalBuffer, &m_normalAllocation, nullptr);

		VkImageViewCreateInfo imageViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
		imageViewCreateInfo.flags = 0;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = 1;
		imageViewCreateInfo.subresourceRange.layerCount = 1;

		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		imageViewCreateInfo.format = VK_FORMAT_D32_SFLOAT;
		imageViewCreateInfo.image = m_depthBuffer;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		result = vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_framebufferViews[0]);

		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.format = VK_FORMAT_R32_UINT;
		imageViewCreateInfo.image = m_depthStorageBuffer;
		result = vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_meshShaderViews[0]);

		imageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageViewCreateInfo.image = m_albedoBuffer;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		result = vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_framebufferViews[1]);
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 1;
		result = vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_meshShaderViews[1]);

		imageViewCreateInfo.image = m_normalBuffer;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		result = vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_framebufferViews[2]);
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 1;
		result = vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_meshShaderViews[2]);

		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		imageViewCreateInfo.subresourceRange.layerCount = 2;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.image = m_albedoBuffer;
		result = vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_combineAndLightViews[0]);
		imageViewCreateInfo.image = m_normalBuffer;
		result = vkCreateImageView(vkDevice, &imageViewCreateInfo, nullptr, &m_combineAndLightViews[1]);
	}

	{
		VkAttachmentDescription attachmentDescription[3];
		attachmentDescription[0].flags = 0;
		attachmentDescription[0].format = VK_FORMAT_D32_SFLOAT;
		attachmentDescription[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescription[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescription[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescription[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachmentDescription[1].flags = 0;
		attachmentDescription[1].format = VK_FORMAT_R8G8B8A8_UNORM;
		attachmentDescription[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescription[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescription[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;;
		attachmentDescription[2].flags = 0;
		attachmentDescription[2].format = VK_FORMAT_R8G8B8A8_UNORM;
		attachmentDescription[2].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescription[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescription[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;;

		VkAttachmentReference colorAttachments[2];
		colorAttachments[0].attachment = 1;
		colorAttachments[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachments[1].attachment = 2;
		colorAttachments[1].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentReference depthAttachment;
		depthAttachment.attachment = 0;
		depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpassDescription;
		subpassDescription.flags = 0;
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.inputAttachmentCount = 0;
		subpassDescription.pInputAttachments = nullptr;
		subpassDescription.colorAttachmentCount = uint32_t(std::size(colorAttachments));
		subpassDescription.pColorAttachments = colorAttachments;
		subpassDescription.pResolveAttachments = 0;
		subpassDescription.pDepthStencilAttachment = &depthAttachment;
		subpassDescription.preserveAttachmentCount = 0;
		subpassDescription.pPreserveAttachments = nullptr;

		VkSubpassDependency subpassDependency[3];
		subpassDependency[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		subpassDependency[0].dstSubpass = 0;
		subpassDependency[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		subpassDependency[0].dstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		subpassDependency[0].srcAccessMask = 0;
		subpassDependency[0].dstAccessMask = 0;
		subpassDependency[0].dependencyFlags = 0;
		subpassDependency[1].srcSubpass = 0;
		subpassDependency[1].dstSubpass = 0;
		subpassDependency[1].srcStageMask = VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV;
		subpassDependency[1].dstStageMask = VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV;
		subpassDependency[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		subpassDependency[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		subpassDependency[1].dependencyFlags = 0;
		subpassDependency[2].srcSubpass = 0;
		subpassDependency[2].dstSubpass = VK_SUBPASS_EXTERNAL;
		subpassDependency[2].srcStageMask = VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpassDependency[2].dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		subpassDependency[2].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		subpassDependency[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		subpassDependency[2].dependencyFlags = 0;

		VkRenderPassCreateInfo renderPassCreateInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr };
		renderPassCreateInfo.flags = 0;
		renderPassCreateInfo.attachmentCount = uint32_t(std::size(attachmentDescription));
		renderPassCreateInfo.pAttachments = attachmentDescription;
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpassDescription;
		renderPassCreateInfo.dependencyCount = uint32_t(std::size(subpassDependency));
		renderPassCreateInfo.pDependencies = subpassDependency;
		result = vkCreateRenderPass(vkDevice, &renderPassCreateInfo, nullptr, &m_renderPass);
	}

	{
		VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr };
		framebufferCreateInfo.flags = 0;
		framebufferCreateInfo.renderPass = m_renderPass;
		framebufferCreateInfo.attachmentCount = uint32_t(std::size(m_framebufferViews));
		framebufferCreateInfo.pAttachments = m_framebufferViews;
		framebufferCreateInfo.width = maxWidth;
		framebufferCreateInfo.height = maxHeight;
		framebufferCreateInfo.layers = 1;
		result = vkCreateFramebuffer(vkDevice, &framebufferCreateInfo, nullptr, &m_framebuffer);
	}

	{
		VkDescriptorSetLayoutBinding descriptorSetLayoutBinding[4];
		descriptorSetLayoutBinding[0].binding = 0;
		descriptorSetLayoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorSetLayoutBinding[0].descriptorCount = 1;
		descriptorSetLayoutBinding[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
		descriptorSetLayoutBinding[0].pImmutableSamplers = nullptr;
		descriptorSetLayoutBinding[1].binding = 1;
		descriptorSetLayoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptorSetLayoutBinding[1].descriptorCount = 1;
		descriptorSetLayoutBinding[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
		descriptorSetLayoutBinding[1].pImmutableSamplers = nullptr;
		descriptorSetLayoutBinding[2].binding = 2;
		descriptorSetLayoutBinding[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptorSetLayoutBinding[2].descriptorCount = 1;
		descriptorSetLayoutBinding[2].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
		descriptorSetLayoutBinding[2].pImmutableSamplers = nullptr;
		descriptorSetLayoutBinding[3].binding = 3;
		descriptorSetLayoutBinding[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptorSetLayoutBinding[3].descriptorCount = 1;
		descriptorSetLayoutBinding[3].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
		descriptorSetLayoutBinding[3].pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
		descriptorSetLayoutCreateInfo.flags = 0;
		descriptorSetLayoutCreateInfo.bindingCount = uint32_t(std::size(descriptorSetLayoutBinding));
		descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBinding;
		result = vkCreateDescriptorSetLayout(vkDevice, &descriptorSetLayoutCreateInfo, nullptr, &m_viewportResourcesLayout);
	}

	{
		VkDescriptorSetLayout descriptorSetLayouts[] = { m_viewportResourcesLayout, device.GetParameterizedMeshDescriptorSetLayout()};

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
		pipelineLayoutCreateInfo.flags = 0;
		pipelineLayoutCreateInfo.setLayoutCount = uint32_t(std::size(descriptorSetLayouts));
		pipelineLayoutCreateInfo.pSetLayouts = descriptorSetLayouts;
		pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
		pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
		result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutCreateInfo, nullptr, &m_graphicPipelineLayout);
	}

	{
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
		descriptorSetAllocateInfo.descriptorPool = device.GetDescriptorPool();
		descriptorSetAllocateInfo.descriptorSetCount = 1;
		descriptorSetAllocateInfo.pSetLayouts = &m_viewportResourcesLayout;
		result = vkAllocateDescriptorSets(vkDevice, &descriptorSetAllocateInfo, &m_viewportResources);

		VkDescriptorBufferInfo bufferInfo;
		bufferInfo.buffer = m_viewportConstantsBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = VK_WHOLE_SIZE;
		VkDescriptorImageInfo imageInfo[3];
		for (uint32_t i = 0; i < std::size(imageInfo); ++i)
		{
			imageInfo[i].sampler = VK_NULL_HANDLE;
			imageInfo[i].imageView = m_meshShaderViews[i];
			imageInfo[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		}

		VkWriteDescriptorSet writeDescriptorSets[4];
		writeDescriptorSets[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
		writeDescriptorSets[0].dstSet = m_viewportResources;
		writeDescriptorSets[0].dstBinding = 0;
		writeDescriptorSets[0].dstArrayElement = 0;
		writeDescriptorSets[0].descriptorCount = 1;
		writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescriptorSets[0].pImageInfo = nullptr;
		writeDescriptorSets[0].pBufferInfo = &bufferInfo;
		writeDescriptorSets[0].pTexelBufferView = nullptr;
		writeDescriptorSets[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
		writeDescriptorSets[1].dstSet = m_viewportResources;
		writeDescriptorSets[1].dstBinding = 1;
		writeDescriptorSets[1].dstArrayElement = 0;
		writeDescriptorSets[1].descriptorCount = 1;
		writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writeDescriptorSets[1].pImageInfo = &imageInfo[0];
		writeDescriptorSets[1].pBufferInfo = nullptr;
		writeDescriptorSets[1].pTexelBufferView = nullptr;
		writeDescriptorSets[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
		writeDescriptorSets[2].dstSet = m_viewportResources;
		writeDescriptorSets[2].dstBinding = 2;
		writeDescriptorSets[2].dstArrayElement = 0;
		writeDescriptorSets[2].descriptorCount = 1;
		writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writeDescriptorSets[2].pImageInfo = &imageInfo[1];
		writeDescriptorSets[2].pBufferInfo = nullptr;
		writeDescriptorSets[2].pTexelBufferView = nullptr;
		writeDescriptorSets[3] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
		writeDescriptorSets[3].dstSet = m_viewportResources;
		writeDescriptorSets[3].dstBinding = 3;
		writeDescriptorSets[3].dstArrayElement = 0;
		writeDescriptorSets[3].descriptorCount = 1;
		writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writeDescriptorSets[3].pImageInfo = &imageInfo[2];
		writeDescriptorSets[3].pBufferInfo = nullptr;
		writeDescriptorSets[3].pTexelBufferView = nullptr;
		vkUpdateDescriptorSets(vkDevice, uint32_t(std::size(writeDescriptorSets)), writeDescriptorSets, 0, nullptr);
	}

	{
		VkPipelineShaderStageCreateInfo depthPipelineStages[2];
		depthPipelineStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
		depthPipelineStages[0].flags = 0;
		depthPipelineStages[0].stage = VK_SHADER_STAGE_TASK_BIT_NV;
		depthPipelineStages[0].module = m_taskShader.GetShaderModule();
		depthPipelineStages[0].pName = "main";
		depthPipelineStages[0].pSpecializationInfo = nullptr;
		depthPipelineStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
		depthPipelineStages[1].flags = 0;
		depthPipelineStages[1].stage = VK_SHADER_STAGE_MESH_BIT_NV;
		depthPipelineStages[1].module = m_depthPassMeshShader.GetShaderModule();
		depthPipelineStages[1].pName = "main";
		depthPipelineStages[1].pSpecializationInfo = nullptr;

		VkPipelineShaderStageCreateInfo gbufferPipelineStages[3];
		gbufferPipelineStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
		gbufferPipelineStages[0].flags = 0;
		gbufferPipelineStages[0].stage = VK_SHADER_STAGE_TASK_BIT_NV;
		gbufferPipelineStages[0].module = m_taskShader.GetShaderModule();
		gbufferPipelineStages[0].pName = "main";
		gbufferPipelineStages[0].pSpecializationInfo = nullptr;
		gbufferPipelineStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
		gbufferPipelineStages[1].flags = 0;
		gbufferPipelineStages[1].stage = VK_SHADER_STAGE_MESH_BIT_NV;
		gbufferPipelineStages[1].module = m_gbufferPassMeshShader.GetShaderModule();
		gbufferPipelineStages[1].pName = "main";
		gbufferPipelineStages[1].pSpecializationInfo = nullptr;
		gbufferPipelineStages[2] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
		gbufferPipelineStages[2].flags = 0;
		gbufferPipelineStages[2].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		gbufferPipelineStages[2].module = m_gbufferPassFragmentShader.GetShaderModule();
		gbufferPipelineStages[2].pName = "main";
		gbufferPipelineStages[2].pSpecializationInfo = nullptr;

		VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr };
		viewportState.flags = 0;
		viewportState.viewportCount = 1;
		viewportState.pViewports = nullptr;
		viewportState.scissorCount = 1;
		viewportState.pScissors = nullptr;

		VkPipelineRasterizationStateCreateInfo rasterizationState{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr };
		rasterizationState.flags = 0;
		rasterizationState.depthClampEnable = VK_FALSE;
		rasterizationState.rasterizerDiscardEnable = VK_FALSE;
		rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationState.depthBiasEnable = VK_FALSE;
		rasterizationState.depthBiasConstantFactor = 0;
		rasterizationState.depthBiasClamp = 0;
		rasterizationState.depthBiasSlopeFactor = 0;
		rasterizationState.lineWidth = 1;

		VkPipelineMultisampleStateCreateInfo multisampleState{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr };
		multisampleState.flags = 0;
		multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampleState.sampleShadingEnable = VK_FALSE;
		multisampleState.minSampleShading = 0;
		multisampleState.pSampleMask = nullptr;
		multisampleState.alphaToCoverageEnable = VK_FALSE;
		multisampleState.alphaToOneEnable = VK_FALSE;

		VkPipelineDepthStencilStateCreateInfo depthPassDepthStencilState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr };
		depthPassDepthStencilState.flags = 0;
		depthPassDepthStencilState.depthTestEnable = VK_TRUE;
		depthPassDepthStencilState.depthWriteEnable = VK_TRUE;
		depthPassDepthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
		depthPassDepthStencilState.depthBoundsTestEnable = VK_FALSE;
		depthPassDepthStencilState.stencilTestEnable = VK_FALSE;
		depthPassDepthStencilState.minDepthBounds = 0;
		depthPassDepthStencilState.maxDepthBounds = 1;

		VkPipelineDepthStencilStateCreateInfo gbufferPassDepthStencilState{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr };
		gbufferPassDepthStencilState.flags = 0;
		gbufferPassDepthStencilState.depthTestEnable = VK_TRUE;
		gbufferPassDepthStencilState.depthWriteEnable = VK_FALSE;
		gbufferPassDepthStencilState.depthCompareOp = VK_COMPARE_OP_EQUAL;
		gbufferPassDepthStencilState.depthBoundsTestEnable = VK_FALSE;
		gbufferPassDepthStencilState.stencilTestEnable = VK_FALSE;
		gbufferPassDepthStencilState.minDepthBounds = 0;
		gbufferPassDepthStencilState.maxDepthBounds = 1;

		VkPipelineColorBlendAttachmentState colorBlendAttachmentState[2];
		colorBlendAttachmentState[0].blendEnable = VK_FALSE;
		colorBlendAttachmentState[0].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachmentState[0].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachmentState[0].colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachmentState[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachmentState[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachmentState[0].alphaBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachmentState[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachmentState[1].blendEnable = VK_FALSE;
		colorBlendAttachmentState[1].srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachmentState[1].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachmentState[1].colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachmentState[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachmentState[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachmentState[1].alphaBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachmentState[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo colorBlendState{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr };
		colorBlendState.flags = 0;
		colorBlendState.logicOpEnable = VK_FALSE;
		colorBlendState.logicOp = VK_LOGIC_OP_COPY;
		colorBlendState.attachmentCount = uint32_t(std::size(colorBlendAttachmentState));
		colorBlendState.pAttachments = colorBlendAttachmentState;
		colorBlendState.blendConstants[0] = 0;
		colorBlendState.blendConstants[1] = 0;
		colorBlendState.blendConstants[2] = 0;
		colorBlendState.blendConstants[3] = 0;

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr };
		dynamicState.flags = 0;
		dynamicState.dynamicStateCount = uint32_t(std::size(dynamicStates));
		dynamicState.pDynamicStates = dynamicStates;

		VkPipelineInputAssemblyStateCreateInfo iaState{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr };
		iaState.flags = 0;
		iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		iaState.primitiveRestartEnable = VK_FALSE;

		VkGraphicsPipelineCreateInfo graphicsPipelineInfo[2];
		graphicsPipelineInfo[0] = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };
		graphicsPipelineInfo[0].flags = 0;
		graphicsPipelineInfo[0].stageCount = uint32_t(std::size(depthPipelineStages));
		graphicsPipelineInfo[0].pStages = depthPipelineStages;
		graphicsPipelineInfo[0].pVertexInputState = nullptr;
		graphicsPipelineInfo[0].pInputAssemblyState = &iaState;
		graphicsPipelineInfo[0].pTessellationState = nullptr;
		graphicsPipelineInfo[0].pViewportState = &viewportState;
		graphicsPipelineInfo[0].pRasterizationState = &rasterizationState;
		graphicsPipelineInfo[0].pMultisampleState = &multisampleState;
		graphicsPipelineInfo[0].pDepthStencilState = &depthPassDepthStencilState;
		graphicsPipelineInfo[0].pColorBlendState = &colorBlendState;
		graphicsPipelineInfo[0].pDynamicState = &dynamicState;
		graphicsPipelineInfo[0].layout = m_graphicPipelineLayout;
		graphicsPipelineInfo[0].renderPass = m_renderPass;
		graphicsPipelineInfo[0].subpass = 0;
		graphicsPipelineInfo[0].basePipelineHandle = VK_NULL_HANDLE;
		graphicsPipelineInfo[0].basePipelineIndex = 0;
		graphicsPipelineInfo[1] = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr };
		graphicsPipelineInfo[1].flags = 0;
		graphicsPipelineInfo[1].stageCount = uint32_t(std::size(gbufferPipelineStages));
		graphicsPipelineInfo[1].pStages = gbufferPipelineStages;
		graphicsPipelineInfo[1].pVertexInputState = nullptr;
		graphicsPipelineInfo[1].pInputAssemblyState = &iaState;
		graphicsPipelineInfo[1].pTessellationState = nullptr;
		graphicsPipelineInfo[1].pViewportState = &viewportState;
		graphicsPipelineInfo[1].pRasterizationState = &rasterizationState;
		graphicsPipelineInfo[1].pMultisampleState = &multisampleState;
		graphicsPipelineInfo[1].pDepthStencilState = &gbufferPassDepthStencilState;
		graphicsPipelineInfo[1].pColorBlendState = &colorBlendState;
		graphicsPipelineInfo[1].pDynamicState = &dynamicState;
		graphicsPipelineInfo[1].layout = m_graphicPipelineLayout;
		graphicsPipelineInfo[1].renderPass = m_renderPass;
		graphicsPipelineInfo[1].subpass = 0;
		graphicsPipelineInfo[1].basePipelineHandle = VK_NULL_HANDLE;
		graphicsPipelineInfo[1].basePipelineIndex = 0;

		// pInputAssemblyState is supposed to be ignored if we use a mesh shader
		// but we get a GPU crash along with a validation error if we don't specify it (probably need to report this):
		// Validation Error: [ UNASSIGNED-CoreValidation-Shader-PointSizeMissing ]
		// false positive? "Pipeline topology is set to POINT_LIST, but PointSize is not written to in the shader corresponding to VK_SHADER_STAGE_MESH_BIT_NV"
		// the mesh shader declares "layout(triangles) out;"
		// POINT_LIST may be assumed default by validation since we don't have a pInputAssemblyState (member is ignored if we use a mesh shader)
		VkPipeline pipelines[uint32_t(std::size(graphicsPipelineInfo))];
		result = vkCreateGraphicsPipelines(vkDevice, VK_NULL_HANDLE, uint32_t(std::size(graphicsPipelineInfo)), graphicsPipelineInfo, nullptr, pipelines);
		m_meshDepthPass = pipelines[0];
		m_meshGbufferPass = pipelines[1];
	}

	{
		VkDescriptorSetLayoutBinding descriptorSetLayoutBinding[4];
		for (uint32_t i = 0; i < std::size(descriptorSetLayoutBinding); ++i)
		{
			descriptorSetLayoutBinding[i].binding = i;
			descriptorSetLayoutBinding[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorSetLayoutBinding[i].descriptorCount = 1;
			descriptorSetLayoutBinding[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			descriptorSetLayoutBinding[i].pImmutableSamplers = &device.GetPointWrapSampler();
		}

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
		descriptorSetLayoutCreateInfo.flags = 0;
		descriptorSetLayoutCreateInfo.bindingCount = uint32_t(std::size(descriptorSetLayoutBinding));
		descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBinding;
		result = vkCreateDescriptorSetLayout(vkDevice, &descriptorSetLayoutCreateInfo, nullptr, &m_combineAndLightResourcesLayout);
	}
	
	{
		VkDescriptorSetLayoutBinding descriptorSetLayoutBinding[1];
		descriptorSetLayoutBinding[0].binding = 0;
		descriptorSetLayoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptorSetLayoutBinding[0].descriptorCount = 1;
		descriptorSetLayoutBinding[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		descriptorSetLayoutBinding[0].pImmutableSamplers = nullptr;

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
		descriptorSetLayoutCreateInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
		descriptorSetLayoutCreateInfo.bindingCount = uint32_t(std::size(descriptorSetLayoutBinding));
		descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBinding;
		result = vkCreateDescriptorSetLayout(vkDevice, &descriptorSetLayoutCreateInfo, nullptr, &m_swapchainResourcesLayout);
	}

	{
		VkDescriptorSetLayout layouts[] = { m_combineAndLightResourcesLayout, m_swapchainResourcesLayout };

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr };
		pipelineLayoutCreateInfo.flags = 0;
		pipelineLayoutCreateInfo.setLayoutCount = uint32_t(std::size(layouts));
		pipelineLayoutCreateInfo.pSetLayouts = layouts;
		pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
		pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;
		result = vkCreatePipelineLayout(vkDevice, &pipelineLayoutCreateInfo, nullptr, &m_combineAndLightPipelineLayout);
	}
	{
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr };
		descriptorSetAllocateInfo.descriptorPool = device.GetDescriptorPool();
		descriptorSetAllocateInfo.descriptorSetCount = 1;
		descriptorSetAllocateInfo.pSetLayouts = &m_combineAndLightResourcesLayout;
		result = vkAllocateDescriptorSets(vkDevice, &descriptorSetAllocateInfo, &m_combineAndLightResources);

		VkDescriptorImageInfo imageInfo[4];
		imageInfo[0].sampler = VK_NULL_HANDLE;
		imageInfo[0].imageView = m_framebufferViews[0];
		imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo[1].sampler = VK_NULL_HANDLE;
		imageInfo[1].imageView = m_meshShaderViews[0];
		imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageInfo[2].sampler = VK_NULL_HANDLE;
		imageInfo[2].imageView = m_combineAndLightViews[0];
		imageInfo[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo[3].sampler = VK_NULL_HANDLE;
		imageInfo[3].imageView = m_combineAndLightViews[1];
		imageInfo[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet writeDescriptorSets[4];
		for (uint32_t i = 0; i < std::size(writeDescriptorSets); ++i)
		{
			writeDescriptorSets[i] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
			writeDescriptorSets[i].dstSet = m_combineAndLightResources;
			writeDescriptorSets[i].dstBinding = i;
			writeDescriptorSets[i].dstArrayElement = 0;
			writeDescriptorSets[i].descriptorCount = 1;
			writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[i].pImageInfo = &imageInfo[i];
			writeDescriptorSets[i].pBufferInfo = nullptr;
			writeDescriptorSets[i].pTexelBufferView = nullptr;
		}

		vkUpdateDescriptorSets(vkDevice, uint32_t(std::size(writeDescriptorSets)), writeDescriptorSets, 0, nullptr);
	}

	{
		VkComputePipelineCreateInfo computePipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr };
		computePipelineInfo.flags = 0;
		computePipelineInfo.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr };
		computePipelineInfo.stage.flags = 0;
		computePipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		computePipelineInfo.stage.module = m_combineAndLightComputeShader.GetShaderModule();
		computePipelineInfo.stage.pName = "main";
		computePipelineInfo.stage.pSpecializationInfo = nullptr;
		computePipelineInfo.layout = m_combineAndLightPipelineLayout;
		computePipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		computePipelineInfo.basePipelineIndex = 0;
		result = vkCreateComputePipelines(vkDevice, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &m_combineAndLight);
	}

	return true;
}

auto MeshShadingRenderLoop::Uninitialize() -> void
{

}

auto MeshShadingRenderLoop::RenderLoop(InstanceDeviceAndSwapchain& deviceAndSwapchain) -> bool
{
	VkCommandBuffer commandBuffer = deviceAndSwapchain.GetCommandBuffer();

	if (!deviceAndSwapchain.AcquireSwapchainImage())
		return false;
	if (!deviceAndSwapchain.HasSwapchain())
		return true;

	VkExtent2D swapchainExtent = deviceAndSwapchain.GetSwapchainExtent();

	// UPDATE CONSTANTS
	{
		ViewportConstants constants;
		constants.projectionMatrix[0][0] = 1; constants.projectionMatrix[0][1] = 0; constants.projectionMatrix[0][2] = 0; constants.projectionMatrix[0][3] = 0;
		constants.projectionMatrix[1][0] = 0; constants.projectionMatrix[1][1] = 1; constants.projectionMatrix[1][2] = 0; constants.projectionMatrix[1][3] = 0;
		constants.projectionMatrix[2][0] = 0; constants.projectionMatrix[2][1] = 0; constants.projectionMatrix[2][2] = 1; constants.projectionMatrix[2][3] = 0.5f;
		constants.projectionMatrix[3][0] = 0; constants.projectionMatrix[3][1] = 0; constants.projectionMatrix[3][2] = 0; constants.projectionMatrix[3][3] = 1;
		
		constants.viewportSize[0] = float(swapchainExtent.width);
		constants.viewportSize[1] = float(swapchainExtent.height);
		constants.viewportSize[2] = 1.0f / float(swapchainExtent.width);
		constants.viewportSize[3] = 1.0f / float(swapchainExtent.height);

		VkBufferMemoryBarrier bufferMemoryBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, nullptr };
		bufferMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		bufferMemoryBarrier.buffer = m_viewportConstantsBuffer;
		bufferMemoryBarrier.offset = 0;
		bufferMemoryBarrier.size = VK_WHOLE_SIZE;

		bufferMemoryBarrier.srcAccessMask = 0;
		bufferMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 1, &bufferMemoryBarrier, 0, nullptr);

		vkCmdUpdateBuffer(commandBuffer, m_viewportConstantsBuffer, 0, sizeof(constants), &constants);

		bufferMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		bufferMemoryBarrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TASK_SHADER_BIT_NV | VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV, 0, 0, nullptr, 1, &bufferMemoryBarrier, 0, nullptr);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicPipelineLayout, 0, 1, &m_viewportResources, 0, nullptr);
	}

	// CLEAR MESH SHADER DEPTH
	{
		VkImageMemoryBarrier imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.image = m_depthStorageBuffer;
		imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
		imageMemoryBarrier.subresourceRange.levelCount = 1;
		imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
		imageMemoryBarrier.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		VkClearColorValue clearColor;
		clearColor.float32[0] = 1;
		clearColor.float32[1] = 1;
		clearColor.float32[2] = 1;
		clearColor.float32[3] = 1;

		vkCmdClearColorImage(commandBuffer, m_depthStorageBuffer, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);

		// TRANSITION DEPTH TO WRITE
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	}

	// TRANSITION MESH SHADER BUFFERS TO WRITE
	{
		VkImageMemoryBarrier imageMemoryBarrier[2];
		for (uint32_t i = 0; i < std::size(imageMemoryBarrier); ++i)
		{
			imageMemoryBarrier[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
			imageMemoryBarrier[i].srcAccessMask = 0;
			imageMemoryBarrier[i].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			imageMemoryBarrier[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
			imageMemoryBarrier[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageMemoryBarrier[i].subresourceRange.baseMipLevel = 0;
			imageMemoryBarrier[i].subresourceRange.levelCount = 1;
			imageMemoryBarrier[i].subresourceRange.baseArrayLayer = 1;
			imageMemoryBarrier[i].subresourceRange.layerCount = 1;
		}
		imageMemoryBarrier[0].image = m_albedoBuffer;
		imageMemoryBarrier[1].image = m_normalBuffer;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, uint32_t(std::size(imageMemoryBarrier)), imageMemoryBarrier);
	}

	// BEGIN RENDER PASS
	{
		VkClearValue clearValue;
		clearValue.depthStencil.depth = 1;

		VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr };
		renderPassBeginInfo.renderPass = m_renderPass;
		renderPassBeginInfo.framebuffer = m_framebuffer;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent = swapchainExtent;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = &clearValue;

		VkViewport viewport;
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = float(swapchainExtent.width);
		viewport.height = float(swapchainExtent.height);
		viewport.minDepth = 0;
		viewport.maxDepth = 1;
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &renderPassBeginInfo.renderArea);

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	// DEPTH PASS
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshDepthPass);

	for (ParameterizedMesh const* mesh : m_meshInstances)
	{
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicPipelineLayout, 1, 1, &mesh->GetDescriptorSet(), 0, nullptr);
		vkCmdDrawMeshTasksNV(commandBuffer, 1, 0);
	}

	// TRANSITION DEPTH
	{
		VkMemoryBarrier memoryBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr };
		memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV, VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV, 0, 1, &memoryBarrier, 0, nullptr, 0, nullptr);
	}

	// GBUFFER PASS
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshGbufferPass);

	for (ParameterizedMesh const* mesh : m_meshInstances)
	{
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicPipelineLayout, 1, 1, &mesh->GetDescriptorSet(), 0, nullptr);
		vkCmdDrawMeshTasksNV(commandBuffer, 1, 0);
	}

	// END RENDER PASS
	vkCmdEndRenderPass(commandBuffer);

	// TRANSITION MESH SHADER BUFFERS TO READ
	{
		VkImageMemoryBarrier imageMemoryBarrier[2];
		for (uint32_t i = 0; i < std::size(imageMemoryBarrier); ++i)
		{
			imageMemoryBarrier[i] = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
			imageMemoryBarrier[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			imageMemoryBarrier[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			imageMemoryBarrier[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			imageMemoryBarrier[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageMemoryBarrier[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageMemoryBarrier[i].subresourceRange.baseMipLevel = 0;
			imageMemoryBarrier[i].subresourceRange.levelCount = 1;
			imageMemoryBarrier[i].subresourceRange.baseArrayLayer = 1;
			imageMemoryBarrier[i].subresourceRange.layerCount = 1;
		}
		imageMemoryBarrier[0].image = m_albedoBuffer;
		imageMemoryBarrier[1].image = m_normalBuffer;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_MESH_SHADER_BIT_NV, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, uint32_t(std::size(imageMemoryBarrier)), imageMemoryBarrier);
	}

	// WAIT FOR SWAPCHAIN
	deviceAndSwapchain.WaitForSwapchainImage();
	commandBuffer = deviceAndSwapchain.GetCommandBuffer();

	// TRANSITION SWAPCHAIN TO WRITE (DISCARD CONTENTS)
	{
		VkImageMemoryBarrier imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.image = deviceAndSwapchain.GetAcquiredImage();
		imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
		imageMemoryBarrier.subresourceRange.levelCount = 1;
		imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
		imageMemoryBarrier.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	}

	// MERGE FRAMBUFFER AND MESH RASTERIZATION IN LIGHTING PASS
	{
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_combineAndLightPipelineLayout, 0, 1, &m_combineAndLightResources, 0, nullptr);
		VkDescriptorImageInfo imageInfo;
		imageInfo.sampler = VK_NULL_HANDLE;
		imageInfo.imageView = deviceAndSwapchain.GetAcquiredImageView();
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		VkWriteDescriptorSet writeDescriptorSets{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr };
		writeDescriptorSets.dstSet = VK_NULL_HANDLE;
		writeDescriptorSets.dstBinding = 0;
		writeDescriptorSets.dstArrayElement = 0;
		writeDescriptorSets.descriptorCount = 1;
		writeDescriptorSets.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writeDescriptorSets.pImageInfo = &imageInfo;
		writeDescriptorSets.pBufferInfo = nullptr;
		writeDescriptorSets.pTexelBufferView = nullptr;
		vkCmdPushDescriptorSetKHR(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_combineAndLightPipelineLayout, 1, 1, &writeDescriptorSets);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_combineAndLight);
		vkCmdDispatch(commandBuffer, (swapchainExtent.width + 7) / 8, (swapchainExtent.height + 7) / 8, 1);
	}

	// TRANSITION SWAPCHAIN TO PRESENT
	{
		VkImageMemoryBarrier imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
		imageMemoryBarrier.srcAccessMask = 0;
		imageMemoryBarrier.dstAccessMask = 0;
		imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		imageMemoryBarrier.image = deviceAndSwapchain.GetAcquiredImage();
		imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
		imageMemoryBarrier.subresourceRange.levelCount = 1;
		imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
		imageMemoryBarrier.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
	}

	return true;
}

auto MeshShadingRenderLoop::AddMeshInstance(ParameterizedMesh const* meshInstance) -> void
{
	m_meshInstances.emplace_back(meshInstance);
}