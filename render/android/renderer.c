#define _POSIX_C_SOURCE 199309L
#include <assert.h>
#include <stdlib.h>
#include "render/android.h"
#include "render/egl.h"
#include "render/gles2.h"
#include <drm_fourcc.h>

static const struct wlr_renderer_impl renderer_impl;

bool wlr_renderer_is_android(struct wlr_renderer *wlr_renderer) {
	return wlr_renderer->impl == &renderer_impl;
}

struct wlr_android_renderer *android_get_renderer(
		struct wlr_renderer *wlr_renderer) {
	assert(wlr_renderer_is_android(wlr_renderer));
	struct wlr_android_renderer *renderer = wl_container_of(wlr_renderer, renderer, wlr_renderer);
	return renderer;
}

static void destroy_buffer(struct wlr_android_buffer *buffer) {
	wl_list_remove(&buffer->link);
	wlr_addon_finish(&buffer->addon);

	free(buffer);
}

static void handle_buffer_destroy(struct wlr_addon *addon) {
	struct wlr_android_buffer *buffer =
		wl_container_of(addon, buffer, addon);
	destroy_buffer(buffer);
}

static const struct wlr_addon_interface buffer_addon_impl = {
	.name = "wlr_android_buffer",
	.destroy = handle_buffer_destroy,
};

static struct wlr_android_buffer *get_or_create_buffer(struct wlr_android_renderer *renderer,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_addon *addon =
		wlr_addon_find(&wlr_buffer->addons, renderer, &buffer_addon_impl);
	if (addon) {
		struct wlr_android_buffer *buffer = wl_container_of(addon, buffer, addon);
		return buffer;
	}

	struct wlr_android_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	buffer->buffer = wlr_buffer;
	buffer->renderer = renderer;

	if (!renderer->window) {
		wlr_log(WLR_ERROR, "No native window set");
		buffer->egl_surface = EGL_NO_SURFACE;
	} else {
		buffer->egl_surface = eglCreateWindowSurface(renderer->egl->display,
			renderer->egl->config, renderer->window, NULL);
		if (buffer->egl_surface == EGL_NO_SURFACE) {
			wlr_log(WLR_ERROR, "Failed to recreate EGL surface");
			return NULL;
		}

		wlr_addon_init(&buffer->addon, &wlr_buffer->addons, renderer,
			&buffer_addon_impl);

		wl_list_insert(&renderer->buffers, &buffer->link);
	}

	wlr_log(WLR_DEBUG, "Created WindowSurface for buffer %dx%d",
			wlr_buffer->width, wlr_buffer->height);

	return buffer;
}

static void android_destroy(struct wlr_renderer *wlr_renderer) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	struct wlr_android_buffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &renderer->buffers, link) {
		destroy_buffer(buffer);
	}

	renderer->wlr_gles_renderer->impl->destroy(renderer->wlr_gles_renderer);
}

static bool android_bind_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	if (wlr_buffer == NULL) {
		return true;
	}

	struct wlr_android_buffer *buffer = get_or_create_buffer(renderer, wlr_buffer);
	if (buffer == NULL) {
		return false;
	}

	return wlr_egl_make_current_with_surface(renderer->egl, buffer->egl_surface);
}

static bool android_begin(struct wlr_renderer *wlr_renderer, uint32_t width,
		uint32_t height) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	return renderer->wlr_gles_renderer->impl->begin(renderer->wlr_gles_renderer, width, height);
}

static void android_end(struct wlr_renderer *wlr_renderer) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	renderer->wlr_gles_renderer->impl->end(renderer->wlr_gles_renderer);
}

static void android_clear(struct wlr_renderer *wlr_renderer, const float color[static 4]) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	renderer->wlr_gles_renderer->impl->clear(renderer->wlr_gles_renderer, color);
}

static void android_scissor(struct wlr_renderer *wlr_renderer, struct wlr_box *box) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	renderer->wlr_gles_renderer->impl->scissor(renderer->wlr_gles_renderer, box);
}

static bool android_render_subtexture_with_matrix(struct wlr_renderer *wlr_renderer,
		struct wlr_texture *wlr_texture, const struct wlr_fbox *box,
		const float matrix[static 9], float alpha) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	return renderer->wlr_gles_renderer->impl->render_subtexture_with_matrix(renderer->wlr_gles_renderer, wlr_texture, box, matrix, alpha);
}

static void android_render_quad_with_matrix(struct wlr_renderer *wlr_renderer,
		const float color[static 4], const float matrix[static 9]) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	renderer->wlr_gles_renderer->impl->render_quad_with_matrix(renderer->wlr_gles_renderer, color, matrix);
}

static const uint32_t *android_get_shm_texture_formats(
		struct wlr_renderer *wlr_renderer, size_t *len) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	return renderer->wlr_gles_renderer->impl->get_shm_texture_formats(renderer->wlr_gles_renderer, len);
}

static const struct wlr_drm_format_set *android_get_dmabuf_texture_formats(
	struct wlr_renderer *wlr_renderer) {
	return NULL;
}

static const struct wlr_drm_format_set *android_get_render_formats(struct wlr_renderer *wlr_renderer) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	return &renderer->shm_formats;
}

static enum wl_shm_format android_preferred_read_format(
		struct wlr_renderer *wlr_renderer) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	return renderer->wlr_gles_renderer->impl->preferred_read_format(renderer->wlr_gles_renderer);
}

static bool android_read_pixels(struct wlr_renderer *wlr_renderer,
		uint32_t drm_format, uint32_t stride,
		uint32_t width, uint32_t height, uint32_t src_x, uint32_t src_y,
		uint32_t dst_x, uint32_t dst_y, void *data) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	return renderer->wlr_gles_renderer->impl->read_pixels(renderer->wlr_gles_renderer, drm_format, stride, width, height, src_x, src_y, dst_x, dst_y, data);
}

static int android_get_drm_fd(struct wlr_renderer *wlr_renderer) {
	return -1;
}

static uint32_t android_get_render_buffer_caps(struct wlr_renderer *wlr_renderer) {
	return WLR_BUFFER_CAP_SHM | WLR_BUFFER_CAP_DATA_PTR;
}

static struct wlr_texture *android_texture_from_buffer(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *wlr_buffer) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	return renderer->wlr_gles_renderer->impl->texture_from_buffer(renderer->wlr_gles_renderer, wlr_buffer);
}

static struct wlr_render_timer *android_render_timer_create(struct wlr_renderer *wlr_renderer) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	return renderer->wlr_gles_renderer->impl->render_timer_create(renderer->wlr_gles_renderer);
}

static void android_set_nativewindow(struct wlr_renderer *wlr_renderer, EGLNativeWindowType window) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	renderer->window = window;
}

static bool android_swap_buffers(struct wlr_renderer *wlr_renderer, pixman_region32_t *damage) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	struct wlr_android_buffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &renderer->buffers, link) {
		if (!buffer || buffer->egl_surface == EGL_NO_SURFACE)
			continue;

		return wlr_egl_swap_buffers(renderer->egl, buffer->egl_surface, buffer->is_damaged ? damage : NULL);
	}

	return true;
}

static bool android_set_damage_region(struct wlr_renderer *wlr_renderer, pixman_region32_t *damage) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	struct wlr_android_buffer *buffer, *buffer_tmp;
	wl_list_for_each_safe(buffer, buffer_tmp, &renderer->buffers, link) {
		if (!buffer || buffer->egl_surface == EGL_NO_SURFACE)
			continue;

		buffer->is_damaged = wlr_egl_set_damage_region(renderer->egl, buffer->egl_surface, damage);
		return buffer->is_damaged;
	}

	return true;
}

static const struct wlr_renderer_impl renderer_impl = {
	.destroy = android_destroy,
	.bind_buffer = android_bind_buffer,
	.begin = android_begin,
	.end = android_end,
	.clear = android_clear,
	.scissor = android_scissor,
	.render_subtexture_with_matrix = android_render_subtexture_with_matrix,
	.render_quad_with_matrix = android_render_quad_with_matrix,
	.get_shm_texture_formats = android_get_shm_texture_formats,
	.get_dmabuf_texture_formats = android_get_dmabuf_texture_formats,
	.get_render_formats = android_get_render_formats,
	.preferred_read_format = android_preferred_read_format,
	.read_pixels = android_read_pixels,
	.get_drm_fd = android_get_drm_fd,
	.get_render_buffer_caps = android_get_render_buffer_caps,
	.texture_from_buffer = android_texture_from_buffer,
	.render_timer_create = android_render_timer_create,
	.set_nativewindow = android_set_nativewindow,
	.swap_buffers = android_swap_buffers,
	.set_damage_region = android_set_damage_region,
};

struct wlr_renderer *wlr_android_renderer_create(void) {
	struct wlr_android_renderer *renderer = calloc(1, sizeof(*renderer));
	if (renderer == NULL) {
		return NULL;
	}

	struct wlr_egl *egl = wlr_egl_create_for_android();
	if (egl == NULL) {
		wlr_log(WLR_ERROR, "Could not initialize EGL");
		return NULL;
	}

	renderer->wlr_gles_renderer = wlr_gles2_renderer_create(egl);
	if (!renderer->wlr_gles_renderer) {
		wlr_log(WLR_ERROR, "Failed to create GLES2 renderer");
		wlr_egl_destroy(egl);
		return NULL;
	}

	wl_list_init(&renderer->buffers);
	wlr_renderer_init(&renderer->wlr_renderer, &renderer_impl);
	renderer->egl = egl;
	wlr_drm_format_set_add(&renderer->shm_formats, DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR);
	wlr_drm_format_set_add(&renderer->shm_formats, DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_LINEAR);

	return &renderer->wlr_renderer;
}

bool android_init_wl_display(struct wlr_renderer *wlr_renderer,
		struct wl_display *wl_display) {
	struct wlr_android_renderer *renderer = android_get_renderer(wlr_renderer);

	if (renderer->egl->exts.bind_wayland_display_wl) {
		if (!wlr_egl_bind_display(renderer->egl, wl_display)) {
			wlr_log(WLR_ERROR, "Failed to bind wl_display to EGL");
			return false;
		}
	} else {
		wlr_log(WLR_INFO, "EGL_WL_bind_wayland_display is not supported");
	}

	return true;
}
