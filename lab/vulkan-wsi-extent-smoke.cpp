#define WIN32_LEAN_AND_MEAN
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <vector>

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int main()
{
    WNDCLASSW wc = {};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"StandaloneNrVulkanExtentSmoke";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Vulkan extent smoke", WS_POPUP,
        0, 0, 3840, 2160, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 2;

    HMODULE vulkan = LoadLibraryW(L"vulkan-1.dll");
    auto get_instance_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkan, "vkGetInstanceProcAddr"));
    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(get_instance_proc(nullptr, "vkCreateInstance"));
    const char *instance_extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Vulkan extent smoke";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = 2;
    instance_info.ppEnabledExtensionNames = instance_extensions;
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create_instance(&instance_info, nullptr, &instance);
    if (result != VK_SUCCESS) { std::printf("vkCreateInstance failed: %d\n", result); return 3; }

#define LOAD_I(name) auto name = reinterpret_cast<PFN_##name>(get_instance_proc(instance, #name))
    LOAD_I(vkCreateWin32SurfaceKHR);
    LOAD_I(vkEnumeratePhysicalDevices);
    LOAD_I(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_I(vkGetPhysicalDeviceSurfaceSupportKHR);
    LOAD_I(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_I(vkGetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_I(vkCreateDevice);
    LOAD_I(vkGetDeviceProcAddr);
    LOAD_I(vkDestroySurfaceKHR);
    LOAD_I(vkDestroyInstance);

    VkWin32SurfaceCreateInfoKHR surface_info = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    surface_info.hinstance = wc.hInstance;
    surface_info.hwnd = hwnd;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    result = vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &surface);
    if (result != VK_SUCCESS) { std::printf("vkCreateWin32SurfaceKHR failed: %d\n", result); return 4; }

    uint32_t physical_count = 0;
    vkEnumeratePhysicalDevices(instance, &physical_count, nullptr);
    std::vector<VkPhysicalDevice> physical_devices(physical_count);
    vkEnumeratePhysicalDevices(instance, &physical_count, physical_devices.data());
    VkPhysicalDevice physical = physical_devices.empty() ? VK_NULL_HANDLE : physical_devices[0];
    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &queue_count, queues.data());
    uint32_t queue_family = UINT32_MAX;
    for (uint32_t index = 0; index < queue_count; ++index)
    {
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physical, index, surface, &supported);
        if (supported && (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT)) { queue_family = index; break; }
    }
    if (queue_family == UINT32_MAX) return 5;

    VkSurfaceCapabilitiesKHR capabilities = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities);
    std::printf("surface current=%ux%u min=%ux%u max=%ux%u\n",
        capabilities.currentExtent.width, capabilities.currentExtent.height,
        capabilities.minImageExtent.width, capabilities.minImageExtent.height,
        capabilities.maxImageExtent.width, capabilities.maxImageExtent.height);
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &format_count, formats.data());

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    VkDevice device = VK_NULL_HANDLE;
    result = vkCreateDevice(physical, &device_info, nullptr, &device);
    if (result != VK_SUCCESS) { std::printf("vkCreateDevice failed: %d\n", result); return 6; }
    auto vkCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
    auto vkDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR"));
    auto vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(vkGetDeviceProcAddr(device, "vkDestroyDevice"));

    VkSwapchainCreateInfoKHR swapchain_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = capabilities.minImageCount;
    swapchain_info.imageFormat = formats[0].format;
    swapchain_info.imageColorSpace = formats[0].colorSpace;
    swapchain_info.imageExtent = {1920, 1080};
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    result = vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain);
    std::printf("1920x1080 swapchain on 3840x2160 client: %d%s\n", result,
        result == VK_SUCCESS ? " (PASS)" : " (REJECTED)");

    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
    DestroyWindow(hwnd);
    FreeLibrary(vulkan);
    return result == VK_SUCCESS ? 0 : 1;
}
