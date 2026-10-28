#ifndef WINDOW_H
#define WINDOW_H

#include <wayland-client.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_wayland.h>
#include "xdg-shell-protocol.h"
#include "xdg_decoration_protocol.h"

class Window
{
public:
    Window(int width, int height);
    ~Window();
    void SetTitle(const char* title);

    void EventLoop();
private:
    static void registry_global(void *data, struct wl_registry *wl_registry, uint32_t name, const char *interface, uint32_t version);
    static void registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name);
    static void xdg_surface_configure(void *data,
                                      struct xdg_surface *xdg_surface,
                                      uint32_t serial);
    static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base,
                                 uint32_t serial);
    static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer);
    static void xdg_toplevel_configure(void *data,
                                       struct xdg_toplevel *toplevel,
                                       int32_t width, int32_t height,
                                       struct wl_array *states);
    static void xdg_toplevel_close(void* data, struct xdg_toplevel* toplevel);

    struct wl_buffer *DrawFrame();
    void InitializeVulkan();

    /* Globals */
    int width, height;
    /* Globals */
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_shm *wl_shm;
    struct wl_compositor *wl_compositor;
    struct xdg_wm_base *xdg_wm_base;
    /* Objects */
    struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct zxdg_decoration_manager_v1 *xdg_decoration_manager;
    struct zxdg_toplevel_decoration_v1 *xdg_toplevel_decoration;
    bool running;
};

#endif // WINDOW_H
