#pragma once
#include "CRSDK/CameraRemote_SDK.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Stateless SDK operations. The REPL serializes commands and owns the handle.
// No SDK calls from a notification callback; it only announces dirty properties.
using ControlLabel = std::function<std::string(unsigned, std::uint64_t)>;
std::string camera_controls(SCRSDK::CrDeviceHandle handle,
    const std::vector<std::string>& arguments, const ControlLabel& label);
