#ifndef RENDER_ANDROID_H
#define RENDER_ANDROID_H

#include <wlr/render/android.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>

struct wlr_android_renderer {
	struct wlr_renderer *wlr_gles_renderer;
	struct wlr_renderer wlr_renderer;

	struct wlr_egl *egl;
	EGLNativeWindowType window;
};

struct wlr_android_renderer *android_get_renderer(
	struct wlr_renderer *wlr_renderer);

#endif
