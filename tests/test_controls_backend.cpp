#include "camera_controls.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>

namespace {
bool writable = true, frames = true, failed_set = false;
bool s1_writable = true, failed_press = false, failed_release = false;
unsigned focus_mode = SCRSDK::CrFocus_AF_S;
std::vector<std::pair<unsigned, std::uint64_t>> writes;
int sets = 0, released = 0, released_lv = 0;
unsigned last_code = 0;
unsigned last_type = 0;
std::uint64_t last_value = 0;
unsigned file_type = SCRSDK::CrFileType_RawJpeg;
bool high_byte = false, malformed_values = false;
}
namespace SCRSDK {
CrError GetSelectDeviceProperties(CrDeviceHandle, CrInt32u, CrInt32u*, CrDeviceProperty** output, CrInt32* count) {
  *count = 21; *output = new CrDeviceProperty[21];
  const unsigned codes[] = {CrDeviceProperty_FocusMode, CrDeviceProperty_FocusArea, CrDeviceProperty_WhiteBalance,
      CrDeviceProperty_DriveMode, CrDeviceProperty_AF_Area_Position, CrDeviceProperty_S1,
      CrDeviceProperty_IsoSensitivity, CrDeviceProperty_ShutterSpeed, CrDeviceProperty_FNumber, CrDeviceProperty_ExposureBiasCompensation,
      CrDeviceProperty_FileType, CrDeviceProperty_CompressionFileFormatStill, CrDeviceProperty_StillImageQuality,
      CrDeviceProperty_ImageSize, CrDeviceProperty_AspectRatio, CrDeviceProperty_RAW_FileCompressionType,
      CrDeviceProperty_ImageStabilizationSteadyShot, CrDeviceProperty_Movie_File_Format,
      CrDeviceProperty_Movie_Recording_FrameRateSetting, CrDeviceProperty_Movie_Recording_Setting,
      CrDeviceProperty_Movie_ImageStabilizationSteadyShot};
  for (int i = 0; i < *count; ++i) {
    auto& property = (*output)[i]; property.SetCode(codes[i]);
    property.SetPropertyEnableFlag(writable ? CrEnableValue_True : CrEnableValue_DisplayOnly);
    if (i == 5 && !s1_writable) property.SetPropertyEnableFlag(CrEnableValue_DisplayOnly);
    property.SetValueType(i == 4 ? CrDataType_UInt32Range : CrDataType_UInt16Array);
    property.SetCurrentValue(i == 0 ? focus_mode : 1);
    property.Alloc(4, 4, 0);
    CrInt16u values[] = {1, 2}; std::memcpy(property.GetValues(), values, sizeof(values));
    if (i == 6 || i == 7) {
      property.SetValueType(CrDataType_UInt32Array); property.Alloc(8, 8, 0);
      const CrInt32u exposure_values[] = {i == 6 ? CrInt32u(0xffffff) : CrInt32u((1 << 16) | 125), i == 6 ? CrInt32u(100) : CrInt32u((1 << 16) | 250)};
      property.SetCurrentValue(exposure_values[0]); std::memcpy(property.GetValues(), exposure_values, sizeof(exposure_values));
    } else if (i == 8) {
      CrInt16u aperture_values[] = {280, 400}; property.SetCurrentValue(280);
      std::memcpy(property.GetValues(), aperture_values, sizeof(aperture_values));
    } else if (i == 9) {
      property.SetValueType(CrDataType_Int16Array); property.SetCurrentValue(static_cast<CrInt32>(-333));
      CrInt16 ev_values[] = {-333, 0}; std::memcpy(property.GetValues(), ev_values, sizeof(ev_values));
    } else if (i == 10) {
      property.SetCurrentValue(file_type); CrInt16u choices[] = {CrFileType_RawJpeg, CrFileType_Jpeg};
      std::memcpy(property.GetValues(), choices, sizeof(choices));
    } else if (i == 11 || i == 16 || i == 18 || i == 20) {
      property.SetValueType(CrDataType_UInt8Array); property.Alloc(2, 2, 0);
      CrInt8u choices[] = {1, 2};
      if (i == 18) { choices[0] = high_byte ? 200 : CrRecordingFrameRateSettingMovie_50p; choices[1] = CrRecordingFrameRateSettingMovie_25p; }
      property.SetCurrentValue(choices[0]); std::memcpy(property.GetValues(), choices, sizeof(choices));
    } else if (i == 19) {
      CrInt16u choices[] = {CrRecordingSettingMovie_100M_422_10bit, CrRecordingSettingMovie_50M_420_10bit};
      property.SetCurrentValue(choices[0]); std::memcpy(property.GetValues(), choices, sizeof(choices));
    }
    if (i == 15 && file_type == CrFileType_Jpeg) property.SetPropertyEnableFlag(CrEnableValue_DisplayOnly);
    if (i == 12 && malformed_values) property.Alloc(3, 3, 0);
  }
  return CrError_None;
}
CrError ReleaseDeviceProperties(CrDeviceHandle, CrDeviceProperty* data) { ++released; delete[] data; return CrError_None; }
CrError GetSelectLiveViewProperties(CrDeviceHandle, CrInt32u, CrInt32u*, CrLiveViewProperty** output, CrInt32* count) {
  if (!frames) { *output = nullptr; *count = 0; return CrError_None; }
  *count = 1; *output = new CrLiveViewProperty[1]; return CrError_None;
}
// Live-view metadata setters are reserved in SDK 2.02. Substitute getters,
// rather than writing private SDK memory or relying on those no-op setters.
CrInt32u CrLiveViewProperty::GetCode() const { return CrLiveViewProperty_AF_Area_Position; }
bool CrLiveViewProperty::IsGetEnableCurrentValue() const { return true; }
CrFrameInfoType CrLiveViewProperty::GetFrameInfoType() const { return CrFrameInfoType_FocusFrameInfo; }
CrInt32u CrLiveViewProperty::GetValueSize() const { return sizeof(CrFocusFrameInfo); }
CrInt8u* CrLiveViewProperty::GetValue() const {
  static CrFocusFrameInfo frame;
  frame.xDenominator = 640; frame.yDenominator = 480; frame.width = 100; frame.height = 80;
  return reinterpret_cast<CrInt8u*>(&frame);
}

CrError ReleaseLiveViewProperties(CrDeviceHandle, CrLiveViewProperty* data) { ++released_lv; delete[] data; return CrError_None; }
CrError SetDeviceProperty(CrDeviceHandle, CrDeviceProperty* property) {
  ++sets; last_code = property->GetCode(); last_value = property->GetCurrentValue();
  last_type = property->GetValueType();
  writes.emplace_back(last_code, last_value);
  if (last_code == CrDeviceProperty_FileType && !failed_set) file_type = last_value;
  if (last_code == CrDeviceProperty_S1 && (last_value == CrLockIndicator_Locked ? failed_press : failed_release)) return CrError_Generic;
  return failed_set ? CrError_Generic : CrError_None;
}
}
class ControlsBackendTests final : public QObject {
  Q_OBJECT
 private:
  QJsonObject command(const std::vector<std::string>& args) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(camera_controls(1, args,
        [](unsigned, std::uint64_t v) { return "Value " + std::to_string(v); }))).object();
  }
 private slots:
  void init() {
    writable = frames = s1_writable = true; failed_set = failed_press = failed_release = false;
    focus_mode = SCRSDK::CrFocus_AF_S; sets = released = released_lv = 0; writes.clear();
    file_type = SCRSDK::CrFileType_RawJpeg; high_byte = malformed_values = false;
  }
  void capabilities_use_camera_choices_and_read_only_flags() {
    const auto result = command({"controls", "list", "test-1"});
    QCOMPARE(result.value("type").toString(), QString("snapshot"));
    const auto properties = result.value("properties").toArray(); QCOMPARE(properties.size(), 19);
    QCOMPARE(properties[0].toObject().value("name").toString(), QString("focus_mode"));
    QCOMPARE(properties[0].toObject().value("values").toArray().size(), 2);
    QVERIFY(properties[0].toObject().value("writable").toBool()); QVERIFY(result.value("af").toBool());
    QCOMPARE(released, 1); QCOMPARE(released_lv, 1);
    writable = false;
    const auto read_only = command({"controls", "list", "test-2"});
    QVERIFY(!read_only.value("properties").toArray()[0].toObject().value("writable").toBool());
    QVERIFY(!read_only.value("af").toBool());
  }
  void values_revalidated_before_setting() {
    const auto code = std::to_string(SCRSDK::CrDeviceProperty_FocusMode);
    QCOMPARE(command({"controls", "set", "one", code, "2"}).value("type").toString(), QString("applied"));
    QCOMPARE(sets, 1); QCOMPARE(last_value, std::uint64_t(2));
    QCOMPARE(command({"controls", "set", "two", code, "999"}).value("type").toString(), QString("error"));
    writable = false;
    QCOMPARE(command({"controls", "set", "three", code, "1"}).value("message").toString(), QString("read-only"));
    QCOMPARE(sets, 1);
  }
  void recording_choices_keep_byte_types_labels_and_revalidate_dependencies() {
    const auto properties = command({"controls", "list", "recording"}).value("properties").toArray();
    auto find = [&properties](const QString& key) { for (const auto& entry : properties) if (entry.toObject().value("name") == key) return entry.toObject(); return QJsonObject(); };
    QCOMPARE(find("file_type").value("current_label").toString(), QString("RAW+JPEG"));
    QCOMPARE(find("movie_framerate").value("current_label").toString(), QString("50p"));
    QCOMPARE(find("movie_setting").value("current_label").toString(), QString("100 Mbps 4:2:2 10-bit"));
    QCOMPARE(find("aspect_ratio").value("current_label").toString(), QString("3:2"));
    const auto rate = std::to_string(SCRSDK::CrDeviceProperty_Movie_Recording_FrameRateSetting);
    QCOMPARE(command({"controls", "set", "rate", rate, std::to_string(SCRSDK::CrRecordingFrameRateSettingMovie_25p)}).value("type").toString(), QString("applied"));
    QCOMPARE(last_type, unsigned(SCRSDK::CrDataType_UInt8Array));
    const auto format = std::to_string(SCRSDK::CrDeviceProperty_FileType);
    QCOMPARE(command({"controls", "set", "format", format, std::to_string(SCRSDK::CrFileType_Jpeg)}).value("type").toString(), QString("applied"));
    QCOMPARE(last_type, unsigned(SCRSDK::CrDataType_UInt16Array));
    const auto count = sets;
    QCOMPARE(command({"controls", "set", "dependent", std::to_string(SCRSDK::CrDeviceProperty_RAW_FileCompressionType), "1"}).value("message").toString(), QString("read-only"));
    QCOMPARE(command({"controls", "set", "destination", std::to_string(SCRSDK::CrDeviceProperty_StillImageStoreDestination), "1"}).value("message").toString(), QString("unsupported"));
    QCOMPARE(command({"controls", "set", "rate-invalid", rate, "999"}).value("message").toString(), QString("invalid-value"));
    QCOMPARE(sets, count);
  }
  void unsigned_bytes_and_malformed_arrays_are_not_misinterpreted() {
    high_byte = true; malformed_values = true;
    const auto properties = command({"controls", "list", "high-byte"}).value("properties").toArray();
    for (const auto& entry : properties) {
      const auto p = entry.toObject();
      if (p.value("name") == "movie_framerate") { QCOMPARE(p.value("values").toArray()[0].toObject().value("value").toString(), QString("200")); QCOMPARE(p.value("current_label").toString(), QString("0xc8")); }
      if (p.value("name") == "image_quality") QVERIFY(!p.value("writable").toBool());
    }
    QCOMPARE(command({"controls", "set", "high", std::to_string(SCRSDK::CrDeviceProperty_Movie_Recording_FrameRateSetting), "200"}).value("type").toString(), QString("applied"));
    QCOMPARE(last_value, std::uint64_t(200)); QCOMPARE(last_type, unsigned(SCRSDK::CrDataType_UInt8Array));
    const auto count = sets;
    QCOMPARE(command({"controls", "set", "malformed", std::to_string(SCRSDK::CrDeviceProperty_StillImageQuality), "1"}).value("type").toString(), QString("error")); QCOMPARE(sets, count);
  }
  void exposure_values_keep_raw_encoding_and_signed_ev() {
    const auto properties = command({"controls", "list", "exposure"}).value("properties").toArray();
    const auto iso = properties[4].toObject(); QCOMPARE(iso.value("name").toString(), QString("iso"));
    QCOMPARE(iso.value("current").toString(), QString("16777215"));
    QCOMPARE(iso.value("values").toArray()[1].toObject().value("value").toString(), QString("100"));
    const auto ev = properties[7].toObject(); QCOMPARE(ev.value("name").toString(), QString("ev"));
    QCOMPARE(ev.value("current").toString(), QString("65203"));
    QCOMPARE(ev.value("values").toArray()[0].toObject().value("value").toString(), QString("65203"));
    const auto iso_code = std::to_string(SCRSDK::CrDeviceProperty_IsoSensitivity);
    QCOMPARE(command({"controls", "set", "iso", iso_code, "100"}).value("type").toString(), QString("applied"));
    QCOMPARE(last_value, std::uint64_t(100)); QCOMPARE(last_type, unsigned(SCRSDK::CrDataType_UInt32));
    const auto ev_code = std::to_string(SCRSDK::CrDeviceProperty_ExposureBiasCompensation);
    QCOMPARE(command({"controls", "set", "ev", ev_code, "65203"}).value("type").toString(), QString("applied"));
    QCOMPARE(last_value, std::uint64_t(CrInt32(-333))); QCOMPARE(last_type, unsigned(SCRSDK::CrDataType_Int16));
    QCOMPARE(command({"controls", "set", "invalid", ev_code, "300"}).value("message").toString(), QString("invalid-value"));
    writable = false;
    QCOMPARE(command({"controls", "set", "locked", iso_code, "100"}).value("message").toString(), QString("read-only"));
  }
  void af_bounds_and_result_errors_are_not_success() {
    QCOMPARE(command({"controls", "af", "one", "320", "240"}).value("type").toString(), QString("applied"));
    QCOMPARE(writes.size(), size_t(3));
    QCOMPARE(writes[0].first, unsigned(SCRSDK::CrDeviceProperty_AF_Area_Position)); QCOMPARE(writes[0].second, std::uint64_t((320 << 16) | 240));
    QCOMPARE(writes[1].first, unsigned(SCRSDK::CrDeviceProperty_S1)); QCOMPARE(writes[1].second, std::uint64_t(SCRSDK::CrLockIndicator_Locked));
    QCOMPARE(writes[2].first, unsigned(SCRSDK::CrDeviceProperty_S1)); QCOMPARE(writes[2].second, std::uint64_t(SCRSDK::CrLockIndicator_Unlocked));
    QCOMPARE(command({"controls", "af", "two", "0", "240"}).value("message").toString(), QString("af-outside"));
    QCOMPARE(command({"controls", "af", "three", "640", "240"}).value("type").toString(), QString("error"));
    QCOMPARE(sets, 3); failed_set = true;
    QCOMPARE(command({"controls", "af", "four", "320", "240"}).value("type").toString(), QString("error"));
    QCOMPARE(writes.size(), size_t(4)); QCOMPARE(writes.back().first, unsigned(SCRSDK::CrDeviceProperty_AF_Area_Position));
    frames = false;
    QCOMPARE(command({"controls", "af", "five", "320", "240"}).value("message").toString(), QString("af-unavailable"));
  }
  void manual_focus_and_unavailable_half_press_never_move_or_start_af() {
    focus_mode = SCRSDK::CrFocus_MF;
    QVERIFY(!command({"controls", "list", "manual"}).value("af").toBool());
    QCOMPARE(command({"controls", "af", "mf", "320", "240"}).value("message").toString(), QString("af-unavailable"));
    focus_mode = SCRSDK::CrFocus_AF_C; s1_writable = false;
    QVERIFY(!command({"controls", "list", "locked"}).value("af").toBool());
    QCOMPARE(command({"controls", "af", "no-s1", "320", "240"}).value("type").toString(), QString("error"));
    QCOMPARE(sets, 0);
  }
  void half_press_failure_still_releases_and_is_not_reported_as_success() {
    failed_press = true;
    const auto result = command({"controls", "af", "press", "320", "240"});
    QCOMPARE(result.value("type").toString(), QString("error"));
    QVERIFY(result.value("message").toString().startsWith("af-press:"));
    QCOMPARE(writes.size(), size_t(3)); QCOMPARE(writes.back().second, std::uint64_t(SCRSDK::CrLockIndicator_Unlocked));
  }
  void release_failure_is_retried_and_visible() {
    failed_release = true;
    const auto result = command({"controls", "af", "release", "320", "240"});
    QCOMPARE(result.value("type").toString(), QString("error"));
    QVERIFY(result.value("message").toString().startsWith("af-release:"));
    QCOMPARE(writes.size(), size_t(4));
    QCOMPARE(writes[2].second, std::uint64_t(SCRSDK::CrLockIndicator_Unlocked));
    QCOMPARE(writes[3].second, std::uint64_t(SCRSDK::CrLockIndicator_Unlocked));
    QCOMPARE(command({"controls", "set", "s1-write", std::to_string(SCRSDK::CrDeviceProperty_S1), "1"}).value("message").toString(), QString("unsupported"));
  }
  void malformed_commands_never_write() {
    QVERIFY(command({"controls", "list", "bad\nrequest"}).isEmpty());
    for (const auto& args : std::vector<std::vector<std::string>>{{"controls", "af", "a", "-1", "0"},
        {"controls", "af", "b", "4294967296", "0"}, {"controls", "set", "c", "4294967297", "1"},
        {"controls", "af", "d", "999999999999999999999999", "0"}})
      QCOMPARE(command(args).value("type").toString(), QString("error"));
    QCOMPARE(sets, 0);
  }
};
QTEST_GUILESS_MAIN(ControlsBackendTests)
#include "test_controls_backend.moc"
