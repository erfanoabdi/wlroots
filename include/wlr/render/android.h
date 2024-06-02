#ifndef WLR_RENDER_ANDROID_H
#define WLR_RENDER_ANDROID_H

#include <wlr/render/wlr_renderer.h>

struct wlr_renderer *wlr_android_renderer_create(void);

bool wlr_renderer_is_android(struct wlr_renderer *wlr_renderer);

#endif
