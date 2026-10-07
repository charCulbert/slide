#pragma once
#include "clap/ext/gui.h"

namespace webview::platform {

extern const char *const windowApi;
extern const bool needsTimer;

// Returns platform data that detach() needs, or nullptr on failure.
void *attach(void *view, const clap_window_t *parent);
void detach(void *view, void *attachment);
void setSize(void *view, uint32_t width, uint32_t height);
void setVisible(void *view, bool visible);
void pumpEvents();

}
