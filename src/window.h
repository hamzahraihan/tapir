#pragma once

#include "app.h"

namespace tapir::window {

bool create(AppState& app, unsigned dpi);
void destroy(AppState& app);
void show(AppState& app);
void hide(AppState& app);
void toggle(AppState& app);

} // namespace tapir::window
