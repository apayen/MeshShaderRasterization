#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#else
#error "platform not supported. please implement me"
#endif

#include "volk/volk.h"
#include "VulkanMemoryAllocator/src/vk_mem_alloc.h"
#include <iostream>
#include <vector>

#define CHECK_ERROR_AND_RETURN(error) if (result != VK_SUCCESS) { std::cerr << error << std::endl; return false; }

class InstanceDeviceAndSwapchain
{
public:
	InstanceDeviceAndSwapchain();
	~InstanceDeviceAndSwapchain();

	auto Initialize(int32_t preferredDeviceIndex, void* platformWindowHandle) -> bool;
	auto Uninitialize() -> void;

	auto GetInstance() const -> VkInstance const& { return m_instance; }
	auto GetDevice() const -> VkDevice const& { return m_device; }
	auto SupportsNvMeshShader() const -> bool { return m_supportsNvMeshShader; }
	auto GetAllocator() const -> VmaAllocator const& { return m_allocator; }
	auto GetPointWrapSampler() const -> VkSampler const& { return m_pointWrapSampler; }
	auto GetDescriptorPool() const -> VkDescriptorPool const& { return m_descriptorPool; }
	auto GetParameterizedMeshDescriptorSetLayout() const -> VkDescriptorSetLayout const& { return m_parameterizedMeshResourcesLayout; }

	auto BeginFrame() -> bool;
	auto AcquireSwapchainImage() -> bool;
	auto WaitForSwapchainImage() -> bool;
	auto EndFrame() -> bool;
	auto WaitIdle() -> bool;
	auto HasSwapchain() const -> bool { return m_swapchain != VK_NULL_HANDLE; }
	auto GetSwapchainExtent() const -> VkExtent2D const& { return m_swapchainExtent; }
	auto GetAcquiredImage() const -> VkImage const& { return m_swapchainImages[m_acquiredImageIndex]; }
	auto GetAcquiredImageView() const -> VkImageView const& { return m_swapchainImageViews[m_acquiredImageIndex]; }
	auto GetCommandBuffer() const -> VkCommandBuffer const& { return m_postWaitForSwapchainImage ? m_frameExecutionContexts[m_currentFrameExecutionContext].m_postAcquireCommandBuffer : m_frameExecutionContexts[m_currentFrameExecutionContext].m_preAcquireCommandBuffer; }

private:
	auto RecreateSwapChain() -> bool;

	VkInstance m_instance;
	VkPhysicalDevice m_physicalDevice;
	VkDevice m_device;
	bool m_supportsNvMeshShader;

	VmaAllocator m_allocator;
	VkSampler m_pointWrapSampler;
	VkDescriptorPool m_descriptorPool;
	VkDescriptorSetLayout m_parameterizedMeshResourcesLayout;

	uint32_t m_queueFamily;
	VkQueue m_queue;

	VkSurfaceKHR m_surface;
	VkFormat m_surfaceFormat;
	std::vector<VkPresentModeKHR> m_presentModes;
	VkPresentModeKHR m_currentPresentMode;

	VkSwapchainKHR m_swapchain;
	VkExtent2D m_swapchainExtent;
	uint32_t m_acquiredImageIndex;
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;

	struct FrameExecutionContext
	{
		VkCommandPool m_commandPool;
		VkFence m_allCommandsCompleted;
		VkSemaphore m_semaphore;
		union
		{
			struct
			{
				VkCommandBuffer m_preAcquireCommandBuffer;
				VkCommandBuffer m_postAcquireCommandBuffer;
			};
			VkCommandBuffer m_commandBuffers[2];
		};

		FrameExecutionContext();

		auto Initialize(VkDevice device, uint32_t queueFamily) -> bool;
		auto Uninitialize(VkDevice device) -> bool;
	};
	std::vector<FrameExecutionContext> m_frameExecutionContexts;
	uint32_t m_currentFrameExecutionContext;
	bool m_postWaitForSwapchainImage;
};