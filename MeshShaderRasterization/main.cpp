#include "InstanceDeviceAndSwapchain.h"

#include <atomic>

std::atomic<bool> g_exitRequested = false;

#ifdef _WIN32
LRESULT CALLBACK WindowProc(_In_ HWND hwnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_DESTROY:
		g_exitRequested = true;
		return 0;
	default:
		break;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
#endif

int main(char* argv[])
{
	int result = 0;

	InstanceDeviceAndSwapchain instanceDeviceAndSwapchain;

	void* platformWindowHandle = nullptr;
#ifdef _WIN32
	HWND hWnd = nullptr;

	WNDCLASSEX wndclass;
	wndclass.cbSize = sizeof(WNDCLASSEX);
	wndclass.style = 0;
	wndclass.lpfnWndProc = &WindowProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = HINSTANCE(GetModuleHandle(nullptr));
	wndclass.hIcon = nullptr;
	wndclass.hCursor = nullptr;
	wndclass.hbrBackground = HBRUSH(1 + COLOR_WINDOW);
	wndclass.lpszMenuName = nullptr;
	wndclass.lpszClassName = "YoloWindowClass";
	wndclass.hIconSm = nullptr;
	if (!RegisterClassEx(&wndclass))
	{
		std::cerr << "failed to register window class" << std::endl;
		result = -2;
		goto end;
	}

	hWnd = CreateWindow(wndclass.lpszClassName, "MeshShaderRasterization", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, nullptr, HINSTANCE(GetModuleHandle(nullptr)), 0);
	if (!hWnd)
	{
		std::cerr << "failed to create window" << std::endl;
		result = -2;
		goto end;
	}
	platformWindowHandle = hWnd;
#else
#error "platform not supported. please implement me"
#endif

	if (!instanceDeviceAndSwapchain.Initialize(0, platformWindowHandle))
	{
		result = -1;
		goto end;
	}

	while (!g_exitRequested.load())
	{
#if _WIN32
		MSG msg;
		int bRet;
		while ((bRet = PeekMessage(&msg, hWnd, 0, 0, PM_REMOVE)) != 0)
		{
			if (bRet == -1) break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
#endif

		instanceDeviceAndSwapchain.BeginFrame();
		if (instanceDeviceAndSwapchain.HasSwapchain())
		{
			instanceDeviceAndSwapchain.WaitForSwapchainImage();

			VkCommandBuffer commandBuffer = instanceDeviceAndSwapchain.GetCommandBuffer();

			VkImageSubresourceRange range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseMipLevel = 0;
			range.levelCount = 1;
			range.baseArrayLayer = 0;
			range.layerCount = 1;

			VkImageMemoryBarrier imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr };
			imageMemoryBarrier.srcAccessMask = 0;
			imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			imageMemoryBarrier.image = instanceDeviceAndSwapchain.GetAcquiredImage();
			imageMemoryBarrier.subresourceRange = range;
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

			VkClearColorValue clearColor;
			clearColor.float32[0] = 1;
			clearColor.float32[1] = 0;
			clearColor.float32[2] = 0;
			clearColor.float32[3] = 1;
			vkCmdClearColorImage(commandBuffer, instanceDeviceAndSwapchain.GetAcquiredImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			imageMemoryBarrier.dstAccessMask = 0;
			imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

			instanceDeviceAndSwapchain.EndFrame();
		}
	}

end:
#ifdef _WIN32
	if (hWnd)
		DestroyWindow(hWnd);
	UnregisterClass(wndclass.lpszClassName, HINSTANCE(GetModuleHandle(nullptr)));
#endif

	system("pause");
	return result;
}