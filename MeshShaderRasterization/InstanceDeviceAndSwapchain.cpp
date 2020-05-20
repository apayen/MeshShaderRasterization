
#define VMA_IMPLEMENTATION
#include "InstanceDeviceAndSwapchain.h"
#include "volk/volk.c" // unorthodox way of not adding volk.c to the project

#include <algorithm>

InstanceDeviceAndSwapchain::InstanceDeviceAndSwapchain()
	: m_instance(VK_NULL_HANDLE)
	, m_device(VK_NULL_HANDLE)
	, m_queue(VK_NULL_HANDLE)
	, m_surface(VK_NULL_HANDLE)
	, m_swapchain(VK_NULL_HANDLE)
	, m_supportsNvMeshShader(false)
{

}

InstanceDeviceAndSwapchain::~InstanceDeviceAndSwapchain()
{
	Uninitialize();
}

auto InstanceDeviceAndSwapchain::Initialize(int32_t preferredDeviceIndex, void* platformWindowHandle) -> bool
{
	VkResult result;

	result = volkInitialize();
	CHECK_ERROR_AND_RETURN("could not initialized volk (vulkan loader)");

	VkApplicationInfo applicationCreateInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr };
	applicationCreateInfo.pApplicationName = "MeshShaderRasterization";
	applicationCreateInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
	applicationCreateInfo.pEngineName = "YoloCoding";
	applicationCreateInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	applicationCreateInfo.apiVersion = VK_API_VERSION_1_1;

	std::vector<char const*> instanceExtensions;
	instanceExtensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef _WIN32
	instanceExtensions.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
#error "platform not supported. please implement me"
#endif

	VkInstanceCreateInfo instanceCreateInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr };
	instanceCreateInfo.flags = 0;
	instanceCreateInfo.pApplicationInfo = &applicationCreateInfo;
	instanceCreateInfo.enabledLayerCount = 0;
	instanceCreateInfo.ppEnabledLayerNames = nullptr;
	instanceCreateInfo.enabledExtensionCount = uint32_t(instanceExtensions.size());
	instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();

	result = vkCreateInstance(&instanceCreateInfo, nullptr, &m_instance);
	CHECK_ERROR_AND_RETURN("could not create instance");

	volkLoadInstance(m_instance);

	uint32_t physicalDeviceCount;
	result = vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, nullptr);
	CHECK_ERROR_AND_RETURN("could not enumerate physical devices");
	std::vector<VkPhysicalDevice> vkPhysicalDevices(physicalDeviceCount);
	result = vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, vkPhysicalDevices.data());
	CHECK_ERROR_AND_RETURN("could not enumerate physical devices");

#if _WIN32
	VkWin32SurfaceCreateInfoKHR surfaceCreateInfo{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, nullptr };
	surfaceCreateInfo.flags = 0;
	surfaceCreateInfo.hinstance = HINSTANCE(GetModuleHandle(nullptr));
	surfaceCreateInfo.hwnd = HWND(platformWindowHandle);

	result = vkCreateWin32SurfaceKHR(m_instance, &surfaceCreateInfo, nullptr, &m_surface);
	CHECK_ERROR_AND_RETURN("could not create surface");
#else
#error "platform not supported. please implement me"
#endif

	struct PhysicalDevice
	{
		VkPhysicalDevice physicalDevice;
		VkPhysicalDeviceProperties physicalDeviceProperties;
		bool supportsNvMeshShader;
		uint32_t preferredQueueFamily;
	};

	std::vector<PhysicalDevice> physicalDevices;
	physicalDevices.reserve(vkPhysicalDevices.size());
	for (VkPhysicalDevice vkPhysicalDevice : vkPhysicalDevices)
	{
		PhysicalDevice physicalDevice;
		physicalDevice.physicalDevice = vkPhysicalDevice;
		physicalDevice.supportsNvMeshShader = false;
		physicalDevice.preferredQueueFamily = UINT32_MAX;
		vkGetPhysicalDeviceProperties(physicalDevice.physicalDevice, &physicalDevice.physicalDeviceProperties);

		uint32_t deviceExtensionCount;
		result = vkEnumerateDeviceExtensionProperties(physicalDevice.physicalDevice, nullptr, &deviceExtensionCount, nullptr);
		CHECK_ERROR_AND_RETURN("could not enumerate physical device extensions");
		std::vector<VkExtensionProperties> deviceExtensionProperties(deviceExtensionCount);
		result = vkEnumerateDeviceExtensionProperties(physicalDevice.physicalDevice, nullptr, &deviceExtensionCount, deviceExtensionProperties.data());
		CHECK_ERROR_AND_RETURN("could not enumerate physical device extensions");

		for (VkExtensionProperties const& extensionProperties : deviceExtensionProperties)
		{
			if (strcmp(extensionProperties.extensionName, VK_NV_MESH_SHADER_EXTENSION_NAME) == 0)
			{
				physicalDevice.supportsNvMeshShader = true;
				break;
			}
		}

		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice.physicalDevice, &queueFamilyCount, nullptr);
		std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice.physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

		for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i)
		{
			if ((queueFamilyProperties[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) != (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
				continue;

			VkBool32 surfaceSupported;
			result = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice.physicalDevice, i, m_surface, &surfaceSupported);
			CHECK_ERROR_AND_RETURN("could not check if a physical device and queue supports the surface");
			if (!surfaceSupported)
				continue;

			VkSurfaceCapabilitiesKHR surfaceCapabilities;
			result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice.physicalDevice, m_surface, &surfaceCapabilities);
			CHECK_ERROR_AND_RETURN("could not check device format capabilities");
			if ((surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) == 0)
				continue;

#ifdef _WIN32
			if (!vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice.physicalDevice, i))
				continue;
#else
#error "platform not supported. please implement me"
#endif

			physicalDevice.preferredQueueFamily = i;
		}

		if (physicalDevice.supportsNvMeshShader && physicalDevice.preferredQueueFamily != UINT32_MAX)
			physicalDevices.emplace_back(physicalDevice);
	}

	if (physicalDevices.empty())
	{
		std::cerr << "no supported physical device found" << std::endl;
		return false;
	}

	std::sort(physicalDevices.begin(), physicalDevices.end(),
		[](PhysicalDevice const &a, PhysicalDevice const &b) -> bool
	{
		auto PhysicalDeviceScore = [](PhysicalDevice const &physicalDevice) -> uint64_t
		{
			uint64_t score = 0;

			switch (physicalDevice.physicalDeviceProperties.deviceType)
			{
			case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score |= 3ull << 62; break;
			case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score |= 2ull << 62; break;
			default: break;
			}

			score |= physicalDevice.physicalDeviceProperties.driverVersion;

			return score;
		};

		return PhysicalDeviceScore(a) > PhysicalDeviceScore(b);
	}
	);

	std::cout << "supported devices: " << physicalDevices.size() << std::endl;
	for (uint32_t i = 0; i < physicalDevices.size(); ++i)
	{
		std::cout << "physicalDevice[" << i << "]: " << physicalDevices[i].physicalDeviceProperties.deviceName << std::endl;
	}
	if (preferredDeviceIndex >= physicalDevices.size())
	{
		std::cerr << "selected physical device index " << preferredDeviceIndex << " is out of range" << std::endl;
		return false;
	}
	std::cout << "using physical device " << preferredDeviceIndex << std::endl;

	PhysicalDevice &physicalDevice = physicalDevices[preferredDeviceIndex];
	m_physicalDevice = physicalDevice.physicalDevice;
	m_supportsNvMeshShader = physicalDevice.supportsNvMeshShader;

	std::vector<char const*> enabledDeviceExtensions;
	enabledDeviceExtensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
	enabledDeviceExtensions.emplace_back(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
	enabledDeviceExtensions.emplace_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	if (m_supportsNvMeshShader)
		enabledDeviceExtensions.emplace_back(VK_NV_MESH_SHADER_EXTENSION_NAME);

	float queuePriorities[] = { 1.0f };
	VkDeviceQueueCreateInfo queueCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr };
	queueCreateInfo.flags = 0;
	queueCreateInfo.queueFamilyIndex = physicalDevice.preferredQueueFamily;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = queuePriorities;

	VkPhysicalDeviceMeshShaderFeaturesNV meshShaderFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_NV, nullptr };
	meshShaderFeatures.taskShader = VK_TRUE;
	meshShaderFeatures.meshShader = VK_TRUE;

	VkPhysicalDeviceShaderFloat16Int8Features float16int8Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT16_INT8_FEATURES_KHR, nullptr };
	float16int8Features.shaderFloat16 = VK_FALSE;
	float16int8Features.shaderInt8 = VK_TRUE;

	VkDeviceCreateInfo deviceCreateInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr };
	deviceCreateInfo.flags = 0;
	deviceCreateInfo.queueCreateInfoCount = 1;
	deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceCreateInfo.enabledLayerCount = 0;
	deviceCreateInfo.ppEnabledLayerNames = nullptr;
	deviceCreateInfo.enabledExtensionCount = uint32_t(enabledDeviceExtensions.size());
	deviceCreateInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();
	deviceCreateInfo.pEnabledFeatures = nullptr;

	// pNext chain (redundant lines, but allows commenting/decommenting)
	meshShaderFeatures.pNext = const_cast<void*>(deviceCreateInfo.pNext);
	deviceCreateInfo.pNext = &meshShaderFeatures;
	float16int8Features.pNext = const_cast<void*>(deviceCreateInfo.pNext);
	deviceCreateInfo.pNext = &float16int8Features;

	result = vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device);
	CHECK_ERROR_AND_RETURN("could not create device");

	volkLoadDevice(m_device);

	m_queueFamily = physicalDevice.preferredQueueFamily;
	vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);

	uint32_t presentModeCount;
	result = vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
	CHECK_ERROR_AND_RETURN("could not check supported present modes");
	m_presentModes.resize(presentModeCount);
	result = vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, m_presentModes.data());
	CHECK_ERROR_AND_RETURN("could not check supported present modes");

	std::sort(m_presentModes.begin(), m_presentModes.end(),
		[](VkPresentModeKHR a, VkPresentModeKHR b)
	{
		auto PresentModeScore = [](VkPresentModeKHR presentMode) -> uint32_t
		{
			switch (presentMode)
			{
			case VK_PRESENT_MODE_FIFO_KHR: return 40;
			case VK_PRESENT_MODE_MAILBOX_KHR: return 30;
			case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return 20;
			case VK_PRESENT_MODE_IMMEDIATE_KHR: return 10;
			default: return 0;
			}
		};

		return PresentModeScore(a) > PresentModeScore(b);
	}
	);
	m_currentPresentMode = m_presentModes[0];

	for (uint32_t i = 0; i < 3; ++i)
	{
		if (!m_frameExecutionContexts.emplace_back().Initialize(m_device, m_queueFamily))
			return false;
	}

	// TODO (this may differ based on monitor/surface)
	m_surfaceFormat = VK_FORMAT_B8G8R8A8_UNORM;

	{
		VmaVulkanFunctions vmaVulkanFunctions;
		vmaVulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
		vmaVulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
		vmaVulkanFunctions.vkAllocateMemory = vkAllocateMemory;
		vmaVulkanFunctions.vkFreeMemory = vkFreeMemory;
		vmaVulkanFunctions.vkMapMemory = vkMapMemory;
		vmaVulkanFunctions.vkUnmapMemory = vkUnmapMemory;
		vmaVulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
		vmaVulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
		vmaVulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
		vmaVulkanFunctions.vkBindImageMemory = vkBindImageMemory;
		vmaVulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
		vmaVulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
		vmaVulkanFunctions.vkCreateBuffer = vkCreateBuffer;
		vmaVulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
		vmaVulkanFunctions.vkCreateImage = vkCreateImage;
		vmaVulkanFunctions.vkDestroyImage = vkDestroyImage;
		vmaVulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;
#if VMA_DEDICATED_ALLOCATION || VMA_VULKAN_VERSION >= 1001000
		vmaVulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2;
		vmaVulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2;
#endif
#if VMA_BIND_MEMORY2 || VMA_VULKAN_VERSION >= 1001000
		vmaVulkanFunctions.vkBindBufferMemory2KHR = vkBindBufferMemory2;
		vmaVulkanFunctions.vkBindImageMemory2KHR = vkBindImageMemory2;
#endif
#if VMA_MEMORY_BUDGET || VMA_VULKAN_VERSION >= 1001000
		vmaVulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
#endif

		VmaAllocatorCreateInfo vmaCreateInfos;
		vmaCreateInfos.flags = VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
		vmaCreateInfos.physicalDevice = m_physicalDevice;
		vmaCreateInfos.device = m_device;
		vmaCreateInfos.preferredLargeHeapBlockSize = 0;
		vmaCreateInfos.pAllocationCallbacks = nullptr;
		vmaCreateInfos.pDeviceMemoryCallbacks = nullptr;
		vmaCreateInfos.frameInUseCount = 0;
		vmaCreateInfos.pHeapSizeLimit = nullptr;
		vmaCreateInfos.pVulkanFunctions = &vmaVulkanFunctions;
		vmaCreateInfos.pRecordSettings = nullptr;
		vmaCreateInfos.instance = m_instance;
		vmaCreateInfos.vulkanApiVersion = VK_API_VERSION_1_1;
		result = vmaCreateAllocator(&vmaCreateInfos, &m_allocator);
		CHECK_ERROR_AND_RETURN("could not initialize vulkan memory allocator");
	}

	{
		VkSamplerCreateInfo samplerCreateInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr };
		samplerCreateInfo.flags = 0;
		samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
		samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.mipLodBias = 0;
		samplerCreateInfo.anisotropyEnable = VK_FALSE;
		samplerCreateInfo.maxAnisotropy = 0;
		samplerCreateInfo.compareEnable = VK_FALSE;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerCreateInfo.minLod = 0;
		samplerCreateInfo.maxLod = 16;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		samplerCreateInfo.unnormalizedCoordinates = VK_FALSE; // spec states can't be used with mips
		result = vkCreateSampler(m_device, &samplerCreateInfo, nullptr, &m_pointWrapSampler);
	}

	{
		// let's create a pool big enough for all we would ever need in this demo
		VkDescriptorPoolSize descriptorPoolSize[3];
		descriptorPoolSize[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorPoolSize[0].descriptorCount = 64;
		descriptorPoolSize[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		descriptorPoolSize[1].descriptorCount = 64;
		descriptorPoolSize[2].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		descriptorPoolSize[2].descriptorCount = 64;
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr };
		descriptorPoolCreateInfo.flags = 0;
		descriptorPoolCreateInfo.maxSets = 64;
		descriptorPoolCreateInfo.poolSizeCount = uint32_t(std::size(descriptorPoolSize));
		descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSize;
		result = vkCreateDescriptorPool(m_device, &descriptorPoolCreateInfo, nullptr, &m_descriptorPool);
		CHECK_ERROR_AND_RETURN("could not create descriptor pool");
	}

	{
		VkDescriptorSetLayoutBinding descriptorSetLayoutBinding[3];
		descriptorSetLayoutBinding[0].binding = 0;
		descriptorSetLayoutBinding[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorSetLayoutBinding[0].descriptorCount = 1;
		descriptorSetLayoutBinding[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
		descriptorSetLayoutBinding[0].pImmutableSamplers = &m_pointWrapSampler;
		descriptorSetLayoutBinding[1].binding = 1;
		descriptorSetLayoutBinding[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorSetLayoutBinding[1].descriptorCount = 1;
		descriptorSetLayoutBinding[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
		descriptorSetLayoutBinding[1].pImmutableSamplers = &m_pointWrapSampler;
		descriptorSetLayoutBinding[2].binding = 2;
		descriptorSetLayoutBinding[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorSetLayoutBinding[2].descriptorCount = 1;
		descriptorSetLayoutBinding[2].stageFlags = VK_SHADER_STAGE_MESH_BIT_NV;
		descriptorSetLayoutBinding[2].pImmutableSamplers = &m_pointWrapSampler;

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr };
		descriptorSetLayoutCreateInfo.flags = 0;
		descriptorSetLayoutCreateInfo.bindingCount = uint32_t(std::size(descriptorSetLayoutBinding));
		descriptorSetLayoutCreateInfo.pBindings = descriptorSetLayoutBinding;
		result = vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutCreateInfo, nullptr, &m_parameterizedMeshResourcesLayout);
	}

	return true;
}

auto InstanceDeviceAndSwapchain::Uninitialize() -> void
{
	if (m_queue)
		vkQueueWaitIdle(m_queue);

	for (FrameExecutionContext& frameExecutionContext : m_frameExecutionContexts)
		frameExecutionContext.Uninitialize(m_device);
	m_frameExecutionContexts.clear();

	vmaDestroyAllocator(m_allocator);
	vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyDevice(m_device, nullptr);
	vkDestroyInstance(m_instance, nullptr);
}

auto InstanceDeviceAndSwapchain::BeginFrame() -> bool
{
	VkResult result;

	result = vkWaitForFences(m_device, 1, &m_frameExecutionContexts[m_currentFrameExecutionContext].m_allCommandsCompleted, VK_TRUE, UINT64_MAX);
	CHECK_ERROR_AND_RETURN("could not wait for fence");
	result = vkResetFences(m_device, 1, &m_frameExecutionContexts[m_currentFrameExecutionContext].m_allCommandsCompleted);
	CHECK_ERROR_AND_RETURN("could not reset fence");

	result = vkResetCommandPool(m_device, m_frameExecutionContexts[m_currentFrameExecutionContext].m_commandPool, 0);
	CHECK_ERROR_AND_RETURN("could not reset command pool");

	m_postWaitForSwapchainImage = false;

	VkCommandBufferBeginInfo commandBufferBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	commandBufferBeginInfo.pInheritanceInfo = nullptr;
	result = vkBeginCommandBuffer(GetCommandBuffer(), &commandBufferBeginInfo);
	CHECK_ERROR_AND_RETURN("could not begin command buffer");

	return true;
}

auto InstanceDeviceAndSwapchain::AcquireSwapchainImage() -> bool
{
	VkResult result;

	if (!HasSwapchain())
		RecreateSwapChain();
	if (!HasSwapchain())
		return true;

	do
	{
		result = vkAcquireNextImageKHR(m_device, m_swapchain, 0, m_frameExecutionContexts[m_currentFrameExecutionContext].m_semaphore, VK_NULL_HANDLE, &m_acquiredImageIndex);
		if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			if (!RecreateSwapChain())
				return false;
			if (!HasSwapchain())
				return true;
		}
		else
			CHECK_ERROR_AND_RETURN("could not acquire next image");
	} while (result != VK_SUCCESS);

	return true;
}

auto InstanceDeviceAndSwapchain::WaitForSwapchainImage() -> bool
{
	VkResult result;

	result = vkEndCommandBuffer(GetCommandBuffer());
	CHECK_ERROR_AND_RETURN("could not end command buffer");

	m_postWaitForSwapchainImage = true;

	VkCommandBufferBeginInfo commandBufferBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr };
	commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	commandBufferBeginInfo.pInheritanceInfo = nullptr;
	result = vkBeginCommandBuffer(GetCommandBuffer(), &commandBufferBeginInfo);
	CHECK_ERROR_AND_RETURN("could not begin command buffer");

	return true;
}

auto InstanceDeviceAndSwapchain::EndFrame() -> bool
{
	VkResult result;

	result = vkEndCommandBuffer(GetCommandBuffer());
	CHECK_ERROR_AND_RETURN("could not end command buffer");

	if (m_postWaitForSwapchainImage)
	{
		VkPipelineStageFlags pipelineStageFlags[] = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };

		VkSubmitInfo submitInfo[2];
		submitInfo[0] = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
		submitInfo[0].waitSemaphoreCount = 0;
		submitInfo[0].pWaitSemaphores = nullptr;
		submitInfo[0].pWaitDstStageMask = nullptr;
		submitInfo[0].commandBufferCount = 1;
		submitInfo[0].pCommandBuffers = &m_frameExecutionContexts[m_currentFrameExecutionContext].m_preAcquireCommandBuffer;
		submitInfo[0].signalSemaphoreCount = 0;
		submitInfo[0].pSignalSemaphores = nullptr;
		submitInfo[1] = { VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
		submitInfo[1].waitSemaphoreCount = 1;
		submitInfo[1].pWaitSemaphores = &m_frameExecutionContexts[m_currentFrameExecutionContext].m_semaphore;
		submitInfo[1].pWaitDstStageMask = pipelineStageFlags;
		submitInfo[1].commandBufferCount = 1;
		submitInfo[1].pCommandBuffers = &m_frameExecutionContexts[m_currentFrameExecutionContext].m_postAcquireCommandBuffer;
		submitInfo[1].signalSemaphoreCount = 1;
		submitInfo[1].pSignalSemaphores = &m_frameExecutionContexts[m_currentFrameExecutionContext].m_semaphore;
		result = vkQueueSubmit(m_queue, uint32_t(std::size(submitInfo)), submitInfo, m_frameExecutionContexts[m_currentFrameExecutionContext].m_allCommandsCompleted);

		VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &m_frameExecutionContexts[m_currentFrameExecutionContext].m_semaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_swapchain;
		presentInfo.pImageIndices = &m_acquiredImageIndex;
		presentInfo.pResults = nullptr;
		result = vkQueuePresentKHR(m_queue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
			; // these results will be handled when acquiring the next image
		else
			CHECK_ERROR_AND_RETURN("could not check device format capabilities");
	}
	else
	{
		VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr };
		submitInfo.waitSemaphoreCount = 0;
		submitInfo.pWaitSemaphores = nullptr;
		submitInfo.pWaitDstStageMask = nullptr;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &m_frameExecutionContexts[m_currentFrameExecutionContext].m_preAcquireCommandBuffer;
		submitInfo.signalSemaphoreCount = 0;
		submitInfo.pSignalSemaphores = nullptr;

		result = vkQueueSubmit(m_queue, 1, &submitInfo, m_frameExecutionContexts[m_currentFrameExecutionContext].m_allCommandsCompleted);
	}

	vkQueueWaitIdle(m_queue);

	++m_currentFrameExecutionContext;
	if (m_currentFrameExecutionContext >= m_frameExecutionContexts.size())
		m_currentFrameExecutionContext = 0;

	return true;
}

auto InstanceDeviceAndSwapchain::WaitIdle() -> bool
{
	VkResult result;
	
	result = vkQueueWaitIdle(m_queue);
	CHECK_ERROR_AND_RETURN("could not wait for queue to be idle");

	return true;
}

auto InstanceDeviceAndSwapchain::RecreateSwapChain() -> bool
{
	VkResult result;

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities);
	CHECK_ERROR_AND_RETURN("could not check device format capabilities");

	VkSwapchainKHR oldSwapchain = m_swapchain;
	m_swapchain = VK_NULL_HANDLE;

	vkQueueWaitIdle(m_queue);

	if (surfaceCapabilities.currentExtent.width == 0 && surfaceCapabilities.currentExtent.height == 0)
	{
		vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);
		return true; // handle windows case where the window is minimized
	}

	VkSwapchainCreateInfoKHR swapchainCreateInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr };
	swapchainCreateInfo.flags = 0;
	swapchainCreateInfo.surface = m_surface;
	swapchainCreateInfo.minImageCount = surfaceCapabilities.minImageCount;
	swapchainCreateInfo.imageFormat = m_surfaceFormat;
	swapchainCreateInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	swapchainCreateInfo.imageExtent = m_swapchainExtent = surfaceCapabilities.currentExtent;
	swapchainCreateInfo.imageArrayLayers = 1;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfo.queueFamilyIndexCount = 0;
	swapchainCreateInfo.pQueueFamilyIndices = nullptr;
	swapchainCreateInfo.preTransform = surfaceCapabilities.currentTransform;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.presentMode = m_currentPresentMode;
	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.oldSwapchain = m_swapchain;
	result = vkCreateSwapchainKHR(m_device, &swapchainCreateInfo, nullptr, &m_swapchain);
	CHECK_ERROR_AND_RETURN("failed creating swapchain");

	uint32_t swapchainImageCount;
	result = vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr);
	CHECK_ERROR_AND_RETURN("failed getting swapchain images");
	m_swapchainImages.resize(swapchainImageCount);
	result = vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, m_swapchainImages.data());
	CHECK_ERROR_AND_RETURN("failed getting swapchain images");

	for (VkImageView imageView : m_swapchainImageViews)
		vkDestroyImageView(m_device, imageView, nullptr);
	for (VkImage swapchainImage : m_swapchainImages)
	{
		VkImageViewCreateInfo imageViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr };
		imageViewCreateInfo.flags = 0;
		imageViewCreateInfo.image = swapchainImage;
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = m_surfaceFormat;
		imageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		imageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = 1;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		result = vkCreateImageView(m_device, &imageViewCreateInfo, nullptr, &m_swapchainImageViews.emplace_back());
		CHECK_ERROR_AND_RETURN("could not create image view");
	}

	if (oldSwapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(m_device, oldSwapchain, nullptr);

	return true;
}

InstanceDeviceAndSwapchain::FrameExecutionContext::FrameExecutionContext()
	: m_commandPool(VK_NULL_HANDLE)
	, m_preAcquireCommandBuffer(VK_NULL_HANDLE)
	, m_postAcquireCommandBuffer(VK_NULL_HANDLE)
	, m_allCommandsCompleted(VK_NULL_HANDLE)
{
}

auto InstanceDeviceAndSwapchain::FrameExecutionContext::Initialize(VkDevice device, uint32_t queueFamily) -> bool
{
	VkResult result;

	VkFenceCreateInfo fenceCreateInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr };
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	result = vkCreateFence(device, &fenceCreateInfo, nullptr, &m_allCommandsCompleted);
	CHECK_ERROR_AND_RETURN("could not create fence");

	VkSemaphoreCreateInfo semaphoreCreateInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr };
	semaphoreCreateInfo.flags = 0;
	vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_semaphore);
	CHECK_ERROR_AND_RETURN("could not create semaphore");

	VkCommandPoolCreateInfo commandPoolCreateInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr };
	commandPoolCreateInfo.flags = 0;
	commandPoolCreateInfo.queueFamilyIndex = queueFamily;
	result = vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &m_commandPool);
	CHECK_ERROR_AND_RETURN("could not create command pool");

	VkCommandBufferAllocateInfo commandBufferAllocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr };
	commandBufferAllocateInfo.commandPool = m_commandPool;
	commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandBufferAllocateInfo.commandBufferCount = uint32_t(std::size(m_commandBuffers));
	result = vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, m_commandBuffers);
	CHECK_ERROR_AND_RETURN("could not allocate command buffers");

	return true;
}

auto InstanceDeviceAndSwapchain::FrameExecutionContext::Uninitialize(VkDevice device) -> bool
{
	VkResult result;

	if (m_allCommandsCompleted != VK_NULL_HANDLE)
	{
		result = vkWaitForFences(device, 1, &m_allCommandsCompleted, VK_TRUE, UINT64_MAX);
		CHECK_ERROR_AND_RETURN("could not wait for fence");
		vkDestroyFence(device, m_allCommandsCompleted, nullptr);
	}

	vkDestroySemaphore(device, m_semaphore, nullptr);
	vkDestroyCommandPool(device, m_commandPool, nullptr);

	return true;
}