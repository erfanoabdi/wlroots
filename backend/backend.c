#include <wayland-server.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/backend/session.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/wayland.h>
#include <wlr/backend/x11.h>
#include <wlr/backend/multi.h>
#include <wlr/util/log.h>

void wlr_backend_init(struct wlr_backend *backend,
		const struct wlr_backend_impl *impl) {
	assert(backend);
	backend->impl = impl;
	wl_signal_init(&backend->events.input_add);
	wl_signal_init(&backend->events.input_remove);
	wl_signal_init(&backend->events.output_add);
	wl_signal_init(&backend->events.output_remove);
}

bool wlr_backend_start(struct wlr_backend *backend) {
	if (backend->impl->start) {
		return backend->impl->start(backend);
	}
	return true;
}

void wlr_backend_destroy(struct wlr_backend *backend) {
	if (backend->impl && backend->impl->destroy) {
		backend->impl->destroy(backend);
	} else {
		free(backend);
	}
}

struct wlr_egl *wlr_backend_get_egl(struct wlr_backend *backend) {
	if (backend->impl->get_egl) {
		return backend->impl->get_egl(backend);
	}
	return NULL;
}

static struct wlr_backend *attempt_wl_backend(struct wl_display *display) {
	struct wlr_backend *backend = wlr_wl_backend_create(display);
	if (backend) {
		int outputs = 1;
		const char *_outputs = getenv("WLR_WL_OUTPUTS");
		if (_outputs) {
			char *end;
			outputs = (int)strtol(_outputs, &end, 10);
			if (*end) {
				wlr_log(L_ERROR, "WLR_WL_OUTPUTS specified with invalid integer, ignoring");
				outputs = 1;
			} else if (outputs < 0) {
				wlr_log(L_ERROR, "WLR_WL_OUTPUTS specified with negative outputs, ignoring");
				outputs = 1;
			}
		}
		while (outputs--) {
			wlr_wl_output_create(backend);
		}
	}
	return backend;
}

struct wlr_backend *wlr_backend_autocreate(struct wl_display *display) {
	struct wlr_backend *backend;
	if (getenv("WAYLAND_DISPLAY") || getenv("_WAYLAND_DISPLAY")) {
		backend = attempt_wl_backend(display);
		if (backend) {
			return backend;
		}
	}

	const char *x11_display = getenv("DISPLAY");
	if (x11_display) {
		return wlr_x11_backend_create(display, x11_display);
	}

	// Attempt DRM+libinput

	struct wlr_session *session = wlr_session_create(display);
	if (!session) {
		wlr_log(L_ERROR, "Failed to start a DRM session");
		return NULL;
	}

	int gpu = wlr_session_find_gpu(session);
	if (gpu == -1) {
		wlr_log(L_ERROR, "Failed to open DRM device");
		goto error_session;
	}

	backend = wlr_multi_backend_create(session);
	if (!backend) {
		goto error_gpu;
	}

	struct wlr_backend *libinput = wlr_libinput_backend_create(display, session);
	if (!libinput) {
		goto error_multi;
	}

	struct wlr_backend *drm = wlr_drm_backend_create(display, session, gpu);
	if (!drm) {
		goto error_libinput;
	}

	wlr_multi_backend_add(backend, libinput);
	wlr_multi_backend_add(backend, drm);
	return backend;

error_libinput:
	wlr_backend_destroy(libinput);
error_multi:
	wlr_backend_destroy(backend);
error_gpu:
	wlr_session_close_file(session, gpu);
error_session:
	wlr_session_destroy(session);
	return NULL;
}
