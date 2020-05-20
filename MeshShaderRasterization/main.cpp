#include "InstanceDeviceAndSwapchain.h"
#include "MeshShadingRenderLoop.h"
#include "ParameterizedMesh.h"

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
	MeshShadingRenderLoop renderLoop;
	ParameterizedMesh parameterizedMesh;

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

	renderLoop.Initialize(instanceDeviceAndSwapchain);
	parameterizedMesh.Initialize(instanceDeviceAndSwapchain);
	renderLoop.AddMeshInstance(&parameterizedMesh);

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
		renderLoop.RenderLoop(instanceDeviceAndSwapchain);
		instanceDeviceAndSwapchain.EndFrame();
	}

end:
#ifdef _WIN32
	if (hWnd)
		DestroyWindow(hWnd);
	UnregisterClass(wndclass.lpszClassName, HINSTANCE(GetModuleHandle(nullptr)));
#endif

	return result;
}