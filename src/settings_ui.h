#pragma once

#include "config.h"
#include "config_writer.h"

#include <windows.h>

#include <vector>

namespace tapir::settings {

struct Result {
    bool ok = false;
    std::vector<config_writer::Value> values;
};

using DoneCallback = void (*)(const Result& result);

bool open(HWND parent, const Settings& current, DoneCallback done);
void close();

} // namespace tapir::settings
