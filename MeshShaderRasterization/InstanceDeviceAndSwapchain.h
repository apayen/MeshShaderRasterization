#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#else
#error "platform not supported. please implement me"
#endif

#include "vulkan/vulkan.h"
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

	auto GetInstance() const -> VkInstance { return m_instance; }
	auto GetDevice() const -> VkDevice { return m_device; }
	auto SupportsNvMeshShader() const -> bool { return m_supportsNvMeshShader; }

	auto BeginFrame() -> bool;
	auto WaitForSwapchainImage() -> bool;
	auto EndFrame() -> bool;
	auto HasSwapchain() const -> bool { return m_swapchain != VK_NULL_HANDLE; }
	auto GetAcquiredImage() const -> VkImage const& { return m_swapchainImages[m_acquiredImageIndex]; }
	auto GetCommandBuffer() const -> VkCommandBuffer const& { return m_postWaitForSwapchainImage ? m_frameExecutionContexts[m_currentFrameExecutionContext].m_postAcquireCommandBuffer : m_frameExecutionContexts[m_currentFrameExecutionContext].m_preAcquireCommandBuffer; }

private:
	auto RecreateSwapChain() -> bool;

	VkInstance m_instance;
	VkPhysicalDevice m_physicalDevice;
	VkDevice m_device;
	bool m_supportsNvMeshShader;

	uint32_t m_queueFamily;
	VkQueue m_queue;

	VkSurfaceKHR m_surface;
	VkFormat m_surfaceFormat;
	std::vector<VkPresentModeKHR> m_presentModes;
	VkPresentModeKHR m_currentPresentMode;

	VkSwapchainKHR m_swapchain;
	uint32_t m_acquiredImageIndex;
	std::vector<VkImage> m_swapchainImages;

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