#include <QCoreApplication>
#include "gallery_widget.h"
#include "image_viewer.h"
#include <QComboBox>
#include <algorithm>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace {
class GalleryFilter final : public QSortFilterProxyModel {
 public:
  using QSortFilterProxyModel::QSortFilterProxyModel;
  QString format;
  void refresh() { invalidateFilter(); }
 protected:
  bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
    const auto index = sourceModel()->index(row, 0, parent);
    const auto type = index.data(GalleryModel::FormatRole).toString();
    const bool matches = format.isEmpty() || (format == "JPEG" ? type.contains("JPEG") :
        (format == "RAW" ? type.contains("RAW") : type == format));
    return matches && QSortFilterProxyModel::filterAcceptsRow(row, parent);
  }
};
QIcon placeholder() {
  QPixmap pixmap(192, 128); pixmap.fill(QColor("#242b36"));
  QPainter painter(&pixmap); painter.setPen(QColor("#aab7c4"));
  painter.drawText(pixmap.rect(), Qt::AlignCenter, QCoreApplication::translate("App", "Фото"));
  return QIcon(pixmap);
}
}
GalleryModel::GalleryModel(QObject* parent) : QAbstractListModel(parent) { pool_.setMaxThreadCount(2); }
GalleryModel::~GalleryModel() { cancel_->store(true); pool_.clear(); pool_.waitForDone(); }
int GalleryModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : items_.size(); }
QVariant GalleryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= items_.size()) return {};
  const auto& item = items_[index.row()];
  switch (role) {
    case Qt::DisplayRole: return item.name + "\n" + item.format();
    case Qt::ToolTipRole: return item.jpeg_path + (item.raw_path.isEmpty() ? "" : '\n' + item.raw_path);
    case KeyRole: return item.key;
    case PathRole: return item.path();
    case SearchRole: return item.name + ' ' + item.jpeg_path + ' ' + item.raw_path;
    case FormatRole: return item.format();
    case DateRole: return item.modified.toMSecsSinceEpoch();
    case Qt::DecorationRole: {
      if (const auto* image = cache_.object(item.signature)) return QIcon(QPixmap::fromImage(*image));
      const_cast<GalleryModel*>(this)->request_thumbnail(index.row());
      static const auto icon = placeholder(); return icon;
    }
    default: return {};
  }
}
bool GalleryModel::set_items(const MediaItems& items) {
  if (items.size() == items_.size()) {
    bool equal = true;
    for (int i = 0; i < items.size(); ++i) if (items[i].signature != items_[i].signature) { equal = false; break; }
    if (equal) return false;
  }
  cancel_->store(true); cancel_ = std::make_shared<std::atomic<bool>>(false);
  ++generation_; pending_.clear();
  beginResetModel(); items_ = items; endResetModel();
  return true;
}
void GalleryModel::request_thumbnail(int row) {
  const auto item = items_[row];
  if (pending_.contains(item.signature) || pending_.size() >= 4) return;
  pending_.insert(item.signature);
  auto* watcher = new QFutureWatcher<MediaResult>(this);
  const auto generation = generation_; const auto cancel = cancel_;
  connect(watcher, &QFutureWatcher<MediaResult>::finished, this, [this, watcher, item, generation] {
    const auto result = watcher->result(); watcher->deleteLater();
    if (generation != generation_) return;
    pending_.remove(item.signature);
    auto image = result.image;
    if (image.isNull()) { image = QImage(192, 128, QImage::Format_RGB32); image.fill(QColor("#403131")); }
    cache_.insert(item.signature, new QImage(image));
    const auto index = std::find_if(items_.begin(), items_.end(), [&item](const auto& i) { return i.key == item.key; });
    if (index != items_.end()) { const auto row = int(index - items_.begin()); emit dataChanged(this->index(row), this->index(row), {Qt::DecorationRole}); }
    emit thumbnail_ready();
  });
  watcher->setFuture(QtConcurrent::run(&pool_, [item, cancel] { return MediaReader::read(item.path(), true, cancel); }));
}
GalleryWidget::GalleryWidget(QWidget* parent) : QWidget(parent), model_(this) {
  auto* layout = new QVBoxLayout(this);
  auto* filters = new QHBoxLayout;
  search_ = new QLineEdit(this); search_->setObjectName("gallery_search"); search_->setPlaceholderText(QCoreApplication::translate("App", "Поиск по имени или пути"));
  format_ = new QComboBox(this); format_->setObjectName("gallery_format"); format_->addItems({QCoreApplication::translate("App", "Все форматы"), "JPEG", "RAW", "RAW+JPEG"});
  sort_ = new QComboBox(this); sort_->addItems({QCoreApplication::translate("App", "Сначала новые"), QCoreApplication::translate("App", "Сначала старые"), QCoreApplication::translate("App", "По имени")});
  filters->addWidget(search_, 1); filters->addWidget(format_); filters->addWidget(sort_);
  layout->addLayout(filters);
  auto* split = new QSplitter(Qt::Horizontal, this);
  list_ = new QListView(split); list_->setObjectName("gallery_list");
  list_->setViewMode(QListView::IconMode); list_->setResizeMode(QListView::Adjust);
  list_->setMovement(QListView::Static); list_->setIconSize({192, 128}); list_->setGridSize({210, 172});
  list_->setUniformItemSizes(true); list_->setMinimumWidth(235);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  auto* proxy = new GalleryFilter(this); proxy_ = proxy;
  proxy->setSourceModel(&model_); proxy->setFilterRole(GalleryModel::SearchRole);
  proxy->setFilterCaseSensitivity(Qt::CaseInsensitive); proxy->setSortRole(GalleryModel::DateRole);
  proxy->sort(0, Qt::DescendingOrder); list_->setModel(proxy);
  auto* details = new QWidget(split); auto* detail_layout = new QVBoxLayout(details);
  auto* open = new QPushButton(QCoreApplication::translate("App", "Открыть в отдельном окне · 100%"), details); open->setObjectName("open_photo_window");
  detail_layout->addWidget(open);
  viewer_ = new ImageViewer(details); viewer_->set_navigation_enabled(true);
  connect(viewer_, &ImageViewer::navigate_requested, this, &GalleryWidget::navigate);
  viewer_->set_caption(QCoreApplication::translate("App", "Выбери снимок в галерее"));
  detail_layout->addWidget(viewer_, 1);
  metadata_ = new QPlainTextEdit(details); metadata_->setObjectName("photo_metadata");
  metadata_->setReadOnly(true); metadata_->setMaximumHeight(145); metadata_->setPlaceholderText(QCoreApplication::translate("App", "EXIF выбранного снимка"));
  detail_layout->addWidget(metadata_);
  split->addWidget(list_); split->addWidget(details); split->setStretchFactor(1, 1); split->setSizes({240, 680});
  layout->addWidget(split, 1);
  count_ = new QLabel(QCoreApplication::translate("App", "Снимков: 0 / 0"), this); layout->addWidget(count_);
  auto count = [this] { count_->setText(QString(QCoreApplication::translate("App", "Снимков: %1 / %2")).arg(proxy_->rowCount()).arg(model_.rowCount())); };
  connect(search_, &QLineEdit::textChanged, this, [this, count](const QString& text) { proxy_->setFilterFixedString(text); count(); });
  connect(format_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, proxy, count](int index) {
    proxy->format = index == 0 ? QString() : format_->currentText(); proxy->refresh(); count();
  });
  connect(sort_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
    proxy_->setSortRole(index == 2 ? GalleryModel::KeyRole : GalleryModel::DateRole);
    proxy_->sort(0, index == 0 ? Qt::DescendingOrder : Qt::AscendingOrder);
  });
  connect(list_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
    const auto path = index.data(GalleryModel::PathRole).toString();
    if (!path.isEmpty()) emit photo_selected(path);
  });
  auto open_current = [this] {
    const auto path = list_->currentIndex().data(GalleryModel::PathRole).toString();
    if (!path.isEmpty()) emit open_requested(path);
  };
  connect(open, &QPushButton::clicked, this, open_current);
  connect(list_, &QListView::doubleClicked, this, [open_current] { open_current(); });
  connect(&model_, &GalleryModel::thumbnail_ready, list_->viewport(), qOverload<>(&QWidget::update));
}
void GalleryWidget::set_items(const MediaItems& items) {
  const auto selected = list_->currentIndex().data(GalleryModel::KeyRole).toString();
  QSignalBlocker guard(list_->selectionModel());
  if (!model_.set_items(items)) return;
  count_->setText(QString(QCoreApplication::translate("App", "Снимков: %1 / %2")).arg(proxy_->rowCount()).arg(model_.rowCount()));
  QModelIndex restore;
  for (int row = 0; row < proxy_->rowCount(); ++row) {
    const auto index = proxy_->index(row, 0);
    if (index.data(GalleryModel::KeyRole).toString() == selected) { restore = index; break; }
  }
  if (restore.isValid()) list_->setCurrentIndex(restore);
  else if (proxy_->rowCount() > 0) {
    list_->setCurrentIndex(proxy_->index(0, 0));

  }
}
void GalleryWidget::show_photo(const MediaResult& result) {
  viewer_->set_image(result.image, QFileInfo(result.path).fileName() + " · " + (result.error.isEmpty() ? result.source : result.error));
  metadata_->setPlainText(result.error.isEmpty() ? result.metadata.join('\n') : result.error);
}

void GalleryWidget::select_path(const QString& path) {
  const QFileInfo file(path);
  const QString key = file.absolutePath() + '/' + file.completeBaseName().toCaseFolded();
  QSignalBlocker guard(list_->selectionModel());
  for (int row = 0; row < proxy_->rowCount(); ++row) {
    const auto index = proxy_->index(row, 0);
    if (index.data(GalleryModel::KeyRole).toString() == key) { list_->setCurrentIndex(index); break; }
  }
}

void GalleryWidget::navigate(int step) {
  const int count = proxy_->rowCount();
  if (!count) return;
  const auto current = list_->currentIndex();
  const int row = current.isValid() ? std::clamp(current.row() + step, 0, count - 1) : step > 0 ? 0 : count - 1;
  list_->setCurrentIndex(proxy_->index(row, 0));
  list_->scrollTo(list_->currentIndex());
}
