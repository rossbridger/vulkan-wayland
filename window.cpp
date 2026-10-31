#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <fstream>
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

void Window::DrawFrame()
{

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

    { // create attachment
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = vk_swapchain_image_format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;


        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        if (vkCreateRenderPass(vk_device, &renderPassInfo, nullptr, &vk_render_pass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render pass!");
        }
    }

    { // create pipeline layout
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 0; // Optional
        pipelineLayoutInfo.pSetLayouts = nullptr; // Optional
        pipelineLayoutInfo.pushConstantRangeCount = 0; // Optional
        pipelineLayoutInfo.pPushConstantRanges = nullptr; // Optional

        if (vkCreatePipelineLayout(vk_device, &pipelineLayoutInfo, nullptr, &vk_pipeline_layout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }

    { // create pipeline
        VkShaderModule vertShaderModule = CreateShaderModule("shader.vert.spv");
        VkShaderModule fragShaderModule = CreateShaderModule("shader.frag.spv");

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;

        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr; // Optional
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr; // Optional

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float) vk_swapchain_extent.width;
        viewport.height = (float) vk_swapchain_extent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = vk_swapchain_extent;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;
        colorBlending.blendConstants[0] = 0.0f; // Optional
        colorBlending.blendConstants[1] = 0.0f; // Optional
        colorBlending.blendConstants[2] = 0.0f; // Optional
        colorBlending.blendConstants[3] = 0.0f; // Optional

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = nullptr; // Optional
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = vk_pipeline_layout;
        pipelineInfo.renderPass = vk_render_pass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipelineInfo.basePipelineIndex = -1; // Optional

        if (vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &vk_graphics_pipeline) != VK_SUCCESS) {
            throw std::runtime_error("failed to create graphics pipeline!");
        }

        vkDestroyShaderModule(vk_device, fragShaderModule, nullptr);
        vkDestroyShaderModule(vk_device, vertShaderModule, nullptr);
    }

    { // create framebuffers
        vk_swapchain_framebuffers.resize(imageCount);
        for (size_t i = 0; i < imageCount; i++) {
            VkImageView attachments[] = {
                vk_swapchain_imageviews[i]
            };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = vk_render_pass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = vk_swapchain_extent.width;
            framebufferInfo.height = vk_swapchain_extent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(vk_device, &framebufferInfo, nullptr, &vk_swapchain_framebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }
}

void Window::DestroyVulkan()
{
    for (auto framebuffer : vk_swapchain_framebuffers)
    {
        vkDestroyFramebuffer(vk_device, framebuffer, nullptr);
    }
    vkDestroyPipeline(vk_device, vk_graphics_pipeline, nullptr);
    vkDestroyPipelineLayout(vk_device, vk_pipeline_layout, nullptr);
    vkDestroyRenderPass(vk_device, vk_render_pass, nullptr);
    for (auto imageView : vk_swapchain_imageviews)
    {
        vkDestroyImageView(vk_device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(vk_device, vk_swapchain, nullptr);
    vkDestroySurfaceKHR(vk_instance, vk_surface, nullptr);
    vkDestroyDevice(vk_device, nullptr);
    vkDestroyInstance(vk_instance, nullptr);
}

VkShaderModule Window::CreateShaderModule(const std::string filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }
    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(vk_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }
    return shaderModule;
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
            break;
        }
        DrawFrame();

        wl_display_roundtrip(wl_display);
    }
}
