#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "window.h"

/* Shared memory support code */
static void randname(char *buf)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int create_shm_file(void)
{
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

static int allocate_shm_file(size_t size)
{
    int fd = create_shm_file();
    if (fd < 0)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void Window::wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    /* Sent by the compositor when it's no longer using this buffer */
    wl_buffer_destroy(wl_buffer);
}

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

    struct wl_buffer *buffer = window->DrawFrame();
    wl_surface_attach(window->wl_surface, buffer, 0, 0);
    wl_surface_commit(window->wl_surface);
}

struct wl_buffer *Window::DrawFrame()
{
    int stride = width * 4;
    int size = stride * height;

    static const struct wl_buffer_listener wl_buffer_listener = {
        .release = &Window::wl_buffer_release,
    };

    int fd = allocate_shm_file(size);
    if (fd == -1) {
        return NULL;
    }

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(wl_shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(
                pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    /* Draw checkerboxed background */
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if ((x + y / 8 * 8) % 16 < 8)
                data[y * width + x] = 0xFF666666;
            else
                data[y * width + x] = 0xFFEEEEEE;
        }
    }

    munmap(data, size);
    wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
    return buffer;
}

void Window::InitializeVulkan()
{

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

Window::Window(int width, int height)
{
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
    xdg_toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(xdg_decoration_manager, xdg_toplevel);
    wl_surface_commit(wl_surface);

    InitializeVulkan();
    running = true;
}

Window::~Window()
{
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

void Window::SetTitle(const char *title)
{
    xdg_toplevel_set_title(xdg_toplevel, title);
    xdg_toplevel_set_app_id(xdg_toplevel, title);
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
