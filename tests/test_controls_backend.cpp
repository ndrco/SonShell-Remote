#include "camera_controls.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstring>

namespace {
bool writable = true, frames = true, failed_set = false;
int sets = 0, released = 0, released_lv = 0;
unsigned last_code = 0;
std::uint64_t last_value = 0;
}
namespace SCRSDK {
CrError GetSelectDeviceProperties(CrDeviceHandle, CrInt32u, CrInt32u*, CrDeviceProperty** output, CrInt32* count) {
  *count = 5; *output = new CrDeviceProperty[5];
  const unsigned codes[] = {CrDeviceProperty_FocusMode, CrDeviceProperty_FocusArea, CrDeviceProperty_WhiteBalance,
      CrDeviceProperty_DriveMode, CrDeviceProperty_AF_Area_Position};
  for (int i = 0; i < 5; ++i) {
    auto& property = (*output)[i]; property.SetCode(codes[i]);
    property.SetPropertyEnableFlag(writable ? CrEnableValue_True : CrEnableValue_DisplayOnly);
    property.SetValueType(i == 4 ? CrDataType_UInt32Range : CrDataType_UInt16Array);
    property.SetCurrentValue(1);
    property.Alloc(4, 4, 0);
    CrInt16u values[] = {1, 2}; std::memcpy(property.GetValues(), values, sizeof(values));
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
  void init() { writable = frames = true; failed_set = false; sets = released = released_lv = 0; }
  void capabilities_use_camera_choices_and_read_only_flags() {
    const auto result = command({"controls", "list", "test-1"});
    QCOMPARE(result.value("type").toString(), QString("snapshot"));
    const auto properties = result.value("properties").toArray(); QCOMPARE(properties.size(), 4);
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
  void af_bounds_and_result_errors_are_not_success() {
    QCOMPARE(command({"controls", "af", "one", "320", "240"}).value("type").toString(), QString("applied"));
    QCOMPARE(last_code, unsigned(SCRSDK::CrDeviceProperty_AF_Area_Position)); QCOMPARE(last_value, std::uint64_t((320 << 16) | 240));
    QCOMPARE(command({"controls", "af", "two", "0", "240"}).value("message").toString(), QString("af-outside"));
    QCOMPARE(command({"controls", "af", "three", "640", "240"}).value("type").toString(), QString("error"));
    QCOMPARE(sets, 1); failed_set = true;
    QCOMPARE(command({"controls", "af", "four", "320", "240"}).value("type").toString(), QString("error"));
    frames = false;
    QCOMPARE(command({"controls", "af", "five", "320", "240"}).value("message").toString(), QString("af-unavailable"));
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
