#include "camera_controls.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <thread>

namespace {
namespace sdk = SCRSDK;
const std::vector<CrInt32u> codes{sdk::CrDeviceProperty_FocusMode, sdk::CrDeviceProperty_FocusArea,
    sdk::CrDeviceProperty_WhiteBalance, sdk::CrDeviceProperty_DriveMode, sdk::CrDeviceProperty_AF_Area_Position,
    sdk::CrDeviceProperty_S1};
std::uint64_t number(const std::string& text) {
  if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("invalid-value");
  size_t used = 0; const auto value = std::stoull(text, &used);
  if (used != text.size()) throw std::runtime_error("invalid-value");
  return value;
}
std::string quoted(const std::string& text) {
  std::string out = "\"";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') out += '\\';
    if (c >= 32) out += char(c);
  }
  return out + '"';
}
struct Properties {
  sdk::CrDeviceHandle handle;
  sdk::CrDeviceProperty* data = nullptr;
  CrInt32 count = 0;
  explicit Properties(sdk::CrDeviceHandle device) : handle(device) {
    auto selected = codes;
    const auto error = sdk::GetSelectDeviceProperties(handle, selected.size(), selected.data(), &data, &count);
    if (error != sdk::CrError_None) {
      if (data) sdk::ReleaseDeviceProperties(handle, data);
      data = nullptr; throw std::runtime_error("read-failed:" + std::to_string(error));
    }
  }
  ~Properties() { if (data) sdk::ReleaseDeviceProperties(handle, data); }
  Properties(const Properties&) = delete;
  Properties& operator=(const Properties&) = delete;
  const sdk::CrDeviceProperty* find(unsigned code) const {
    for (int i = 0; i < count; ++i) if (data[i].GetCode() == code) return &data[i];
    return nullptr;
  }
};
std::vector<std::uint64_t> choices(const sdk::CrDeviceProperty& property) {
  const auto type = property.GetValueType();
  const int size = type == sdk::CrDataType_UInt16Array ? 2 : type == sdk::CrDataType_UInt32Array ? 4 : 0;
  const auto* bytes = property.GetValues(); const auto length = property.GetValueSize();
  if (!size || !bytes || length % size || length > 4096) return {};
  std::vector<std::uint64_t> out;
  for (unsigned offset = 0; offset < length; offset += size) {
    std::uint16_t small = 0; std::uint32_t large = 0;
    if (size == 2) { std::memcpy(&small, bytes + offset, 2); out.push_back(small); }
    else { std::memcpy(&large, bytes + offset, 4); out.push_back(large); }
  }
  return out;
}
struct Bounds { int left = 0, top = 0, right = 639, bottom = 479; bool valid = false; };
bool focus_available(const Properties& properties) {
  const auto* mode = properties.find(sdk::CrDeviceProperty_FocusMode);
  const auto* half_press = properties.find(sdk::CrDeviceProperty_S1);
  if (!mode || !mode->IsGetEnableCurrentValue() || !half_press || !half_press->IsSetEnableCurrentValue()) return false;
  switch (mode->GetCurrentValue()) {
    case sdk::CrFocus_AF_S: case sdk::CrFocus_AF_C: case sdk::CrFocus_AF_A:
    case sdk::CrFocus_AF_D: case sdk::CrFocus_DMF: return true;
    default: return false;
  }
}
struct HalfPress {
  sdk::CrDeviceHandle handle;
  bool armed = false;
  sdk::CrError release() {
    sdk::CrDeviceProperty property;
    property.SetCode(sdk::CrDeviceProperty_S1); property.SetValueType(sdk::CrDataType_UInt16);
    property.SetCurrentValue(sdk::CrLockIndicator_Unlocked);
    const auto error = sdk::SetDeviceProperty(handle, &property);
    if (error == sdk::CrError_None) armed = false;
    return error;
  }
  ~HalfPress() { if (armed) release(); }
};
void focus_once(sdk::CrDeviceHandle handle) {
  HalfPress press{handle, true}; // Also release after an ambiguous failed press.
  sdk::CrDeviceProperty property;
  property.SetCode(sdk::CrDeviceProperty_S1); property.SetValueType(sdk::CrDataType_UInt16);
  property.SetCurrentValue(sdk::CrLockIndicator_Locked);
  const auto error = sdk::SetDeviceProperty(handle, &property);
  if (error != sdk::CrError_None) throw std::runtime_error("af-press:" + std::to_string(error));
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  const auto released = press.release();
  if (released != sdk::CrError_None) throw std::runtime_error("af-release:" + std::to_string(released));
}
Bounds af_bounds(sdk::CrDeviceHandle handle) {
  sdk::CrLiveViewProperty* data = nullptr; CrInt32 count = 0;
  CrInt32u code = sdk::CrLiveViewProperty_AF_Area_Position;
  const auto error = sdk::GetSelectLiveViewProperties(handle, 1, &code, &data, &count);
  auto release = [handle](sdk::CrLiveViewProperty* p) { if (p) sdk::ReleaseLiveViewProperties(handle, p); };
  std::unique_ptr<sdk::CrLiveViewProperty, decltype(release)> owner(data, release);
  Bounds bounds;
  if (error != sdk::CrError_None || !data) return bounds;
  for (int i = 0; i < count; ++i) {
    const auto& property = data[i];
    if (property.GetCode() != code || !property.IsGetEnableCurrentValue() ||
        property.GetFrameInfoType() != sdk::CrFrameInfoType_FocusFrameInfo || !property.GetValue()) continue;
    const auto length = property.GetValueSize();
    if (length % sizeof(sdk::CrFocusFrameInfo) || length > 1024 * sizeof(sdk::CrFocusFrameInfo)) continue;
    for (unsigned offset = 0; offset < length; offset += sizeof(sdk::CrFocusFrameInfo)) {
      sdk::CrFocusFrameInfo frame;
      std::memcpy(&frame, property.GetValue() + offset, sizeof(frame));
      if (!frame.xDenominator || !frame.yDenominator || !frame.width || !frame.height ||
          frame.width >= frame.xDenominator || frame.height >= frame.yDenominator) continue;
      // Intersection is conservative when the SDK returns more than one AF frame.
      const auto x = int(std::ceil(640.0 * frame.width / (2.0 * frame.xDenominator)));
      const auto y = int(std::ceil(480.0 * frame.height / (2.0 * frame.yDenominator)));
      bounds.left = std::max(bounds.left, x); bounds.right = std::min(bounds.right, 639 - x);
      bounds.top = std::max(bounds.top, y); bounds.bottom = std::min(bounds.bottom, 479 - y);
      bounds.valid = bounds.left <= bounds.right && bounds.top <= bounds.bottom;
    }
  }
  return bounds;
}
}
std::string camera_controls(sdk::CrDeviceHandle handle, const std::vector<std::string>& args, const ControlLabel& label) {
  if (args.size() < 3 || args[2].empty() || args[2].size() > 80 ||
      args[2].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") != std::string::npos) return {};
  const auto request = args[2];
  try {
    Properties properties(handle);
    if (args[1] == "list" && args.size() == 3) {
      std::ostringstream out; out << "{\"type\":\"snapshot\",\"request\":" << quoted(request) << ",\"properties\":[";
      bool first = true;
      for (auto code : codes) {
        if (code == sdk::CrDeviceProperty_AF_Area_Position || code == sdk::CrDeviceProperty_S1) continue;
        const auto* property = properties.find(code); if (!property) continue;
        const auto values = choices(*property);
        const std::string name = code == sdk::CrDeviceProperty_FocusMode ? "focus_mode" : code == sdk::CrDeviceProperty_FocusArea ? "focus_area" : code == sdk::CrDeviceProperty_WhiteBalance ? "white_balance" : "drive_mode";
        if (!first) out << ',';
        first = false;
        out << "{\"name\":" << quoted(name) << ",\"code\":" << code << ",\"readable\":" << (property->IsGetEnableCurrentValue() ? "true" : "false")
            << ",\"writable\":" << (property->IsSetEnableCurrentValue() && !values.empty() ? "true" : "false")
            << ",\"current\":" << quoted(std::to_string(property->GetCurrentValue())) << ",\"values\":[";
        bool first_value = true;
        for (auto value : values) {
          if (!first_value) out << ',';
          first_value = false;
          out << "{\"value\":" << quoted(std::to_string(value)) << ",\"label\":" << quoted(label(code, value)) << '}';
        }
        out << "]}";
      }
      const auto* position = properties.find(sdk::CrDeviceProperty_AF_Area_Position);
      const bool af = position && position->IsSetEnableCurrentValue() && af_bounds(handle).valid && focus_available(properties);
      out << "],\"af\":" << (af ? "true" : "false") << '}';
      if (out.str().size() > 29000) throw std::runtime_error("capabilities-too-large");
      return out.str();
    }
    sdk::CrDeviceProperty change;
    if (args[1] == "set" && args.size() == 5) {
      const auto raw_code = number(args[3]);
      if (raw_code > 0xffffffffu || raw_code == sdk::CrDeviceProperty_AF_Area_Position || raw_code == sdk::CrDeviceProperty_S1) throw std::runtime_error("unsupported");
      const auto* property = properties.find(unsigned(raw_code));
      if (!property || !property->IsSetEnableCurrentValue()) throw std::runtime_error("read-only");
      const auto value = number(args[4]); const auto values = choices(*property);
      if (std::find(values.begin(), values.end(), value) == values.end()) throw std::runtime_error("invalid-value");
      change.SetCode(unsigned(raw_code)); change.SetValueType(property->GetValueType()); change.SetCurrentValue(value);
    } else if (args[1] == "af" && args.size() == 5) {
      const auto* position = properties.find(sdk::CrDeviceProperty_AF_Area_Position);
      const auto bounds = af_bounds(handle);
      if (!position || !position->IsSetEnableCurrentValue() || !bounds.valid) throw std::runtime_error("af-unavailable");
      const auto x = number(args[3]), y = number(args[4]);
      if (x < unsigned(bounds.left) || x > unsigned(bounds.right) || y < unsigned(bounds.top) || y > unsigned(bounds.bottom))
        throw std::runtime_error("af-outside");
      if (!focus_available(properties)) throw std::runtime_error("af-unavailable");
      change.SetCode(sdk::CrDeviceProperty_AF_Area_Position); change.SetValueType(sdk::CrDataType_UInt32);
      change.SetCurrentValue((x << 16) | y);
    } else throw std::runtime_error("invalid-command");
    const auto error = sdk::SetDeviceProperty(handle, &change);
    if (error != sdk::CrError_None) throw std::runtime_error("sdk:" + std::to_string(error));
    if (args[1] == "af") focus_once(handle);
    return "{\"type\":\"applied\",\"request\":" + quoted(request) + "}";
  } catch (const std::exception& error) {
    return "{\"type\":\"error\",\"request\":" + quoted(request) + ",\"message\":" + quoted(error.what()) + "}";
  }
}
