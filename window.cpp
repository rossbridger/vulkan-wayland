#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include "window.h"

void Window::xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, wl_array *states)
{
    Window *window = static_cast<Window *>(data);
    if (width != 0 && height != 0)
    {
        window->width = width;
        window->height = height;
    }
}

void Window::xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    Window *window = static_cast<Window *>(data);
    window->running = false;
}

void Window::xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
    Window *window = static_cast<Window *>(data);
    xdg_surface_ack_configure(xdg_surface, serial);
}

void Window::InitializeVulkan()
{
    { // create vulkan instance
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = title;
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "No Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;

        const char *requiredExtensions[] = {
            "VK_KHR_surface",
            "VK_KHR_wayland_surface",
        };

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = sizeof(requiredExtensions) / sizeof(const char *);
        createInfo.ppEnabledExtensionNames = requiredExtensions;
        createInfo.enabledLayerCount = 0;
        createInfo.ppEnabledLayerNames = nullptr;
        vkCreateInstance(&createInfo, nullptr, &vk_instance);
    }

    { // create vulkan physical device
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(vk_instance, &deviceCount, nullptr);
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(vk_instance, &deviceCount, devices.data());
        vk_physical_device = devices[0]; // Simplify the device selection process since i only have integrated card
    }

    uint32_t queueFamilyIndex = 0;
    {
        static const char* const deviceExtensionNames[] = {
            "VK_KHR_swapchain"
        };

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;
        createInfo.enabledExtensionCount = sizeof(deviceExtensionNames) / sizeof(const char*);
        createInfo.ppEnabledExtensionNames = deviceExtensionNames;

        createInfo.pEnabledFeatures = &deviceFeatures;
        if (vkCreateDevice(vk_physical_device, &createInfo, nullptr, &vk_device) != VK_SUCCESS)
        { // better use macro for it
            std::cerr << "Creating device failed" << std::endl;
            abort();
        }
    }

    { // get queue
        vkGetDeviceQueue(vk_device, queueFamilyIndex, 0, &vk_queue);
    }

    { // create vulkan surface
        VkWaylandSurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.display = wl_display;
        surfaceCreateInfo.surface = wl_surface;
        if (vkCreateWaylandSurfaceKHR(vk_instance, &surfaceCreateInfo, nullptr, &vk_surface) != VK_SUCCESS)
        {
            std::cerr << "Creating surface failed" << std::endl;
            abort();
        }
    }

    uint32_t imageCount = 3;
    { // create swapchain
        // probably should check vkGetPhysicalDeviceSurfaceCapabilitiesKHR first
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        vk_swapchain_extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
        vk_swapchain_image_format = VK_FORMAT_B8G8R8A8_SRGB;

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = vk_surface;

        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = vk_swapchain_image_format;
        createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        createInfo.imageExtent = vk_swapchain_extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // we use the same queue index here
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
        createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;
        if (vkCreateSwapchainKHR(vk_device, &createInfo, nullptr, &vk_swapchain) != VK_SUCCESS)
        {
            std::cerr << "Creating swapchain failed" << std::endl;
            abort();
        }
        vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &imageCount, nullptr);
        vk_swapchain_images.resize(imageCount);
        vkGetSwapchainImagesKHR(vk_device, vk_swapchain, &imageCount, vk_swapchain_images.data());
    }

    { // create image views
        vk_swapchain_imageviews.resize(imageCount);

        for (size_t i = 0; i < imageCount; i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = vk_swapchain_images[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = vk_swapchain_image_format;
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            vkCreateImageView(vk_device, &createInfo, nullptr, &vk_swapchain_imageviews[i]);
        }
    }
}

void Window::DestroyVulkan()
{
    for (auto imageView : vk_swapchain_imageviews)
    {
        vkDestroyImageView(vk_device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(vk_device, vk_swapchain, nullptr);
    vkDestroySurfaceKHR(vk_instance, vk_surface, nullptr);
    vkDestroyDevice(vk_device, nullptr);
    vkDestroyInstance(vk_instance, nullptr);
}

void Window::xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

void Window::registry_global(void *data, struct wl_registry *wl_registry,
                             uint32_t name, const char *interface,
                             uint32_t version)
{
    static const struct xdg_wm_base_listener xdg_wm_base_listener = {
        .ping = &Window::xdg_wm_base_ping,
    };

    Window *window = static_cast<Window *>(data);

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        window->wl_shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        window->wl_compositor =
                wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        window->xdg_wm_base =
                wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(window->xdg_wm_base, &xdg_wm_base_listener, data);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        window->xdg_decoration_manager =
                wl_registry_bind(wl_registry, name, &zxdg_decoration_manager_v1_interface, 1);
    }
}

void Window::registry_global_remove(void *data, struct wl_registry *wl_registry,
                                    uint32_t name)
{
    /* This space deliberately left blank */
}

Window::Window(const char *title, int width, int height)
{
    this->title = title;
    this->width = width;
    this->height = height;

    static const struct wl_registry_listener wl_registry_listener = {
        .global = &Window::registry_global,
        .global_remove = &Window::registry_global_remove,
    };

    static const struct xdg_surface_listener xdg_surface_listener = {
        .configure = &Window::xdg_surface_configure,
    };

    static const struct xdg_toplevel_listener xdg_toplevel_listener = {
        .configure = &Window::xdg_toplevel_configure,
        .close = &Window::xdg_toplevel_close,
    };
    wl_display = wl_display_connect(NULL);
    wl_registry = wl_display_get_registry(wl_display);
    wl_registry_add_listener(wl_registry, &wl_registry_listener, this);
    wl_display_roundtrip(wl_display);

    wl_surface = wl_compositor_create_surface(wl_compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, wl_surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, this);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, this);
    xdg_toplevel_set_title(xdg_toplevel, title);
    xdg_toplevel_set_app_id(xdg_toplevel, title);
    xdg_toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(xdg_decoration_manager, xdg_toplevel);
    wl_surface_commit(wl_surface);

    InitializeVulkan();
    running = true;
}

Window::~Window()
{
    DestroyVulkan();
    zxdg_toplevel_decoration_v1_destroy(xdg_toplevel_decoration);
    zxdg_decoration_manager_v1_destroy(xdg_decoration_manager);
    xdg_toplevel_destroy(xdg_toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(wl_surface);
    xdg_wm_base_destroy(xdg_wm_base);
    wl_compositor_destroy(wl_compositor);
    wl_registry_destroy(wl_registry);
    wl_display_disconnect(wl_display);
}

void Window::EventLoop()
{
    while (running) {
        if (!wl_display_dispatch(wl_display))
        {
            running = false;
        }
    }
}
