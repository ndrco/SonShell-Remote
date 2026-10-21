#include "camera_browser.h"
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QTemporaryDir>
#include <atomic>
#include <cstring>

// Link-time SDK substitutes. Constructors/destructors use the local SDK runtime;
// no SDK Init, camera, network or hardware calls are made by these tests.
namespace {
CameraBrowser* browser = nullptr;
std::atomic<int> originals{0}, previews{0}, deletions{0}, cancels{0}, path_style{1}, slot2_lists{0};
std::atomic<bool> slot2_present{false}, slot2_ready{false};
std::atomic<bool> hold_transfer{false}, deletion_fails{false}, changed_identity{false};
std::atomic<bool> preview_fails{false}, short_file{false};
std::atomic<bool> delete_enabled{true}, protected_file{false};
}
namespace SCRSDK {
CrError GetDeviceProperties(CrDeviceHandle, CrDeviceProperty** output, CrInt32* count) {
  *count = 6; *output = new CrDeviceProperty[6];
  const unsigned codes[] = {CrDeviceProperty_DeleteContentOperationEnableStatusSLOT1, CrDeviceProperty_DeleteContentOperationEnableStatusSLOT2,
      CrDeviceProperty_MediaSLOT1_ContentsInfoListEnableStatus, CrDeviceProperty_MediaSLOT2_ContentsInfoListEnableStatus,
      CrDeviceProperty_MediaSLOT1_Status, CrDeviceProperty_MediaSLOT2_Status};
  for (int i = 0; i < 6; ++i) {
    auto& property = (*output)[i]; property.SetCode(codes[i]); property.SetValueType(CrDataType_UInt16);
    property.SetPropertyEnableFlag(i == 5 && !slot2_present ? CrEnableValue_NotSupported : CrEnableValue_True);
    property.SetCurrentValue(i < 2 ? (delete_enabled ? 1 : 0) : i == 2 ? 1 : i == 3 ? (slot2_ready ? 1 : 0) : i == 5 ? CrSlotStatus_NoCard : CrSlotStatus_OK);
  }
  return CrError_None;
}
CrError ReleaseDeviceProperties(CrDeviceHandle, CrDeviceProperty* properties) { delete[] properties; return CrError_None; }
CrError GetRemoteTransferContentsInfoList(CrDeviceHandle, CrSlotNumber slot, CrGetContentsInfoListType, CrCaptureDate*, CrInt32u, CrContentsInfo** output, CrInt32u* count) {
  if (slot == CrSlotNumber_Slot2) { ++slot2_lists; *count = 0; *output = nullptr; return CrError_None; }
  *count = 1; *output = new CrContentsInfo[1]; auto& content = **output;
  content.contentId = 17; content.protectionStatus = protected_file; content.dummyContent = false;
  content.creationDatetimeUTC.year = changed_identity ? 2025 : 2026; content.creationDatetimeUTC.month = 10; content.creationDatetimeUTC.day = 21;
  content.creationDatetimeUTC.hour = 12; content.creationDatetimeUTC.minute = 0; content.creationDatetimeUTC.sec = 0; content.creationDatetimeUTC.msec = 0;
  content.filesNum = 2; content.files = new CrContentsFile[2];
  for (int i = 0; i < 2; ++i) {
    auto& file = content.files[i]; file.fileId = i + 1; file.fileFormat = i ? CrContentsFile_FileFormat_Raw : CrContentsFile_FileFormat_Jpeg;
    const auto relative = i ? "DCIM/100MSDCF/DSC001.ARW" : "DCIM/100MSDCF/DSC001.JPG";
    std::string path = path_style == 1 ? std::string("C:/") + relative : path_style == 2 ? std::string("memory:0:/") + relative :
        path_style == 3 ? std::string("D:/") + relative : path_style == 4 ? std::string("C:/../") + relative : std::string(relative);
    if (path_style == 3) std::replace(path.begin(), path.end(), '/', '\\');
    file.filePathLength = path.size() + 1; file.filePath = new char[file.filePathLength]; std::strcpy(file.filePath, path.c_str()); file.fileSize = 8;
  }
  return CrError_None;
}
CrError ReleaseRemoteTransferContentsInfoList(CrDeviceHandle, CrContentsInfo* list) { delete[] list; return CrError_None; }
static CrError write_transfer(CrChar* directory, CrChar* name) {
  if (!hold_transfer) {
    QFile file(QDir(QString::fromUtf8(directory)).filePath(QString::fromUtf8(name)));
    if (!file.open(QIODevice::WriteOnly)) return CrError_Generic;
    file.write(short_file ? "1234" : "12345678"); file.close(); browser->transfer_result(CrNotify_RemoteTransfer_Result_OK, 100);
  }
  return CrError_None;
}
CrError GetRemoteTransferContentsDataFile(CrDeviceHandle, CrSlotNumber, CrInt32u, CrInt32u, CrInt32u, CrChar* path, CrChar* name) {
  ++originals; return write_transfer(path, name);
}
CrError GetRemoteTransferContentsCompressedDataFile(CrDeviceHandle, CrSlotNumber, CrInt32u, CrInt32u, CrGetContentsCompressedDataType type, CrChar* path, CrChar* name) {
  if (type != CrGetContentsCompressedDataType_Screennail && type != CrGetContentsCompressedDataType_Thumbnail) return CrError_Generic;
  ++previews; if (preview_fails) return CrError_Generic;
  return write_transfer(path, name);
}
CrError ControlGetRemoteTransferContentsDataFile(CrDeviceHandle, CrGetContentsDataControlType type) {
  if (type == CrGetContentsDataControlType_Cancel) { ++cancels; browser->transfer_result(CrNotify_RemoteTransfer_Control_Canceled, 0); }
  return CrError_None;
}
CrError DeleteRemoteTransferContentsFile(CrDeviceHandle, CrSlotNumber slot, CrInt32u content) {
  ++deletions;
  browser->delete_result(CrWarningExt_DeleteContent, deletion_fails ? CrWarningExtParam_DeleteContentResult_ContentProtected : CrWarningExtParam_DeleteContentResult_OK, content, slot);
  return CrError_None;
}
}

class CardBackendTests final : public QObject {
  Q_OBJECT
 private:
  QTemporaryDir temporary_;
  std::mutex transfers_;
  QMutex events_mutex_;
  QVector<QJsonObject> events_;
  std::unique_ptr<CameraBrowser> service_;
  bool has(const QString& type, const QString& request) {
    QMutexLocker lock(&events_mutex_);
    for (const auto& event : events_) if (event.value("type") == type && event.value("request") == request) return true;
    return false;
  }
  void list() { QVERIFY(service_->command({"card", "list", "catalog"})); QTRY_VERIFY(has("end", "catalog")); }
  std::vector<std::string> command(const std::string& kind, const std::string& request, const std::string& parameter = {}) {
    std::vector<std::string> args{"card", kind, request, "catalog", "1", "17"};
    if (kind != "delete") { args.push_back("2"); args.push_back(parameter); }
    return args;
  }
 private slots:
  void init() {
    preview_fails = short_file = false; path_style = 1; slot2_lists = 0; slot2_present = slot2_ready = false;
    originals = previews = deletions = cancels = 0; hold_transfer = deletion_fails = changed_identity = protected_file = false; delete_enabled = true;
    events_.clear();
    service_ = std::make_unique<CameraBrowser>(transfers_, [this](const std::string& json) {
      QMutexLocker lock(&events_mutex_); events_.append(QJsonDocument::fromJson(QByteArray::fromStdString(json)).object());
    });
    browser = service_.get(); service_->start(1, temporary_.filePath("cache").toStdString(), "body-serial");
  }
  void cleanup() { service_->stop(); service_.reset(); browser = nullptr; }
  void notification_and_previews_never_download_raw() {
    service_->contents_changed(); QTest::qWait(30); QCOMPARE(originals.load(), 0); QCOMPARE(previews.load(), 0);
    list(); QCOMPARE(originals.load(), 0);
    QVERIFY(service_->command(command("preview", "preview1", "screen")));
    QTRY_VERIFY(has("preview", "preview1")); QCOMPARE(previews.load(), 1); QCOMPARE(originals.load(), 0);
    preview_fails = true;
    QVERIFY(service_->command(command("preview", "unsupported", "screen")));
    QTRY_VERIFY(has("error", "unsupported")); QCOMPARE(originals.load(), 0);
  }
  void originals_only_by_request_and_cancel_acknowledged() {
    list(); hold_transfer = true;
    const auto root = temporary_.filePath("copies");
    const auto destination = root.toUtf8().toHex().toStdString();
    QVERIFY(service_->command(command("copy", "copy1", destination))); QTRY_COMPARE(originals.load(), 1);
    QVERIFY(service_->command({"card", "cancel"})); QTRY_VERIFY(has("error", "copy1")); QCOMPARE(cancels.load(), 1);
    QVERIFY(!QFileInfo::exists(root + "/Slot_1/DCIM/100MSDCF/DSC001.ARW"));
    hold_transfer = false;
    QVERIFY(service_->command(command("copy", "copy2", destination))); QTRY_VERIFY(has("copied", "copy2"));
    QCOMPARE(QFileInfo(root + "/Slot_1/DCIM/100MSDCF/DSC001.ARW").size(), 8);
    QVERIFY(service_->command(command("copy", "copy3", destination))); QTRY_VERIFY(has("copied", "copy3"));
    QVERIFY(QFileInfo::exists(root + "/Slot_1/DCIM/100MSDCF/DSC001_1.ARW"));
  }
  void changed_card_and_incomplete_original_are_rejected() {
    list(); const auto root = temporary_.filePath("verified"); QVERIFY(QDir().mkpath(root));
    const auto destination = root.toUtf8().toHex().toStdString();
    changed_identity = true; service_->contents_changed();
    QVERIFY(service_->command(command("copy", "reused-id", destination)));
    QTRY_VERIFY(has("error", "reused-id")); QCOMPARE(originals.load(), 0);
    changed_identity = false; short_file = true;
    QVERIFY(service_->command(command("copy", "short", destination)));
    QTRY_VERIFY(has("error", "short")); QVERIFY(!has("copied", "short"));
    QVERIFY(!QFileInfo::exists(root + "/Slot_1/DCIM/100MSDCF/DSC001.ARW"));
    short_file = false;
    auto stale = command("copy", "stale", destination); stale[3] = "other-catalog";
    QVERIFY(service_->command(stale)); QTRY_VERIFY(has("error", "stale")); QCOMPARE(originals.load(), 1);
    const auto linked = temporary_.filePath("linked"); QVERIFY(QDir().mkpath(linked));
    QVERIFY(QFile::link(root, linked + "/Slot_1"));
    QVERIFY(service_->command(command("copy", "symlink", linked.toUtf8().toHex().toStdString())));
    QTRY_VERIFY(has("error", "symlink")); QCOMPARE(originals.load(), 1);
  }
  void camera_volume_paths_data() {
    QTest::addColumn<int>("style");
    QTest::newRow("plain") << 0; QTest::newRow("drive") << 1;
    QTest::newRow("namespace") << 2; QTest::newRow("backslashes") << 3;
  }
  void camera_volume_paths() {
    QFETCH(int, style); path_style = style; list();
    const auto root = temporary_.filePath("path-" + QString::number(style));
    QVERIFY(service_->command(command("copy", "volume", root.toUtf8().toHex().toStdString())));
    QTRY_VERIFY(has("copied", "volume"));
    QCOMPARE(QFileInfo(root + "/Slot_1/DCIM/100MSDCF/DSC001.ARW").size(), 8);
    path_style = 4;
    QVERIFY(service_->command({"card", "list", "unsafe-catalog"})); QTRY_VERIFY(has("end", "unsafe-catalog"));
    auto unsafe = command("copy", "unsafe", root.toUtf8().toHex().toStdString()); unsafe[3] = "unsafe-catalog";
    QVERIFY(service_->command(unsafe)); QTRY_VERIFY(has("error", "unsafe")); QCOMPARE(originals.load(), 1);
  }
  void unsupported_second_slot_is_silent_supported_empty_slot_is_reported() {
    list(); QVERIFY(!has("slot-error", "catalog")); QCOMPARE(slot2_lists.load(), 0);
    slot2_present = true;
    QVERIFY(service_->command({"card", "list", "empty-slot"})); QTRY_VERIFY(has("end", "empty-slot"));
    QVERIFY(has("slot-error", "empty-slot")); QCOMPARE(slot2_lists.load(), 0);
    slot2_ready = true;
    QVERIFY(service_->command({"card", "list", "ready-slot"})); QTRY_VERIFY(has("end", "ready-slot"));
    QVERIFY(!has("slot-error", "ready-slot")); QCOMPARE(slot2_lists.load(), 1);
  }
  void delete_requires_current_identity_capability_and_terminal_success() {
    list(); changed_identity = true;
    QVERIFY(service_->command(command("delete", "changed"))); QTRY_VERIFY(has("error", "changed")); QCOMPARE(deletions.load(), 0);
    changed_identity = false; delete_enabled = false;
    QVERIFY(service_->command(command("delete", "disabled"))); QTRY_VERIFY(has("error", "disabled")); QCOMPARE(deletions.load(), 0);
    delete_enabled = true; protected_file = true;
    QVERIFY(service_->command(command("delete", "protected"))); QTRY_VERIFY(has("error", "protected")); QCOMPARE(deletions.load(), 0);
    protected_file = false; deletion_fails = true;
    QVERIFY(service_->command(command("delete", "failed"))); QTRY_VERIFY(has("error", "failed")); QVERIFY(!has("deleted", "failed"));
    deletion_fails = false;
    QVERIFY(service_->command(command("delete", "success"))); QTRY_VERIFY(has("deleted", "success")); QCOMPARE(deletions.load(), 2);
  }
};
QTEST_GUILESS_MAIN(CardBackendTests)
#include "test_card_backend.moc"
