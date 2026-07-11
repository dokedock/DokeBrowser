#include "ProfileListModel.h"

ProfileListModel::ProfileListModel(QObject* parent) : QAbstractListModel(parent) {}

int ProfileListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_items.size();
}

QVariant ProfileListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
    return {};
  }

  const auto& item = m_items.at(index.row());
  switch (role) {
    case IdRole:
      return item.id;
    case NameRole:
    case Qt::DisplayRole:
      return item.name;
    case GroupRole:
      return item.group;
    case RemarkRole:
      return item.remark;
    case StatusRole:
      return item.status;
    case CreatedAtMsRole:
      return item.createdAtMs;
    case LastOpenAtMsRole:
      return item.lastOpenAtMs;
    default:
      return {};
  }
}

QHash<int, QByteArray> ProfileListModel::roleNames() const {
  return {
      {IdRole, "profileId"},
      {NameRole, "profileName"},
      {GroupRole, "profileGroup"},
      {RemarkRole, "profileRemark"},
      {StatusRole, "profileStatus"},
      {CreatedAtMsRole, "profileCreatedAtMs"},
      {LastOpenAtMsRole, "profileLastOpenAtMs"},
  };
}

const QVector<ProfileListModel::ProfileItem>& ProfileListModel::items() const {
  return m_items;
}

int ProfileListModel::addProfile(const ProfileItem& item) {
  const int row = m_items.size();
  beginInsertRows(QModelIndex(), row, row);
  m_items.push_back(item);
  endInsertRows();
  return row;
}

void ProfileListModel::removeAt(int index) {
  if (index < 0 || index >= m_items.size()) {
    return;
  }
  beginRemoveRows(QModelIndex(), index, index);
  m_items.removeAt(index);
  endRemoveRows();
}

void ProfileListModel::updateAt(int index, const ProfileItem& item) {
  if (index < 0 || index >= m_items.size()) {
    return;
  }
  m_items[index] = item;
  emit dataChanged(this->index(index), this->index(index));
}

bool ProfileListModel::setStatus(int index, const QString& status) {
  if (index < 0 || index >= m_items.size()) {
    return false;
  }
  auto& it = m_items[index];
  if (it.status == status) {
    return true;
  }
  it.status = status;
  emit dataChanged(this->index(index), this->index(index), {StatusRole});
  return true;
}

bool ProfileListModel::setLastOpenAtMs(int index, qint64 ms) {
  if (index < 0 || index >= m_items.size()) {
    return false;
  }
  auto& it = m_items[index];
  if (it.lastOpenAtMs == ms) {
    return true;
  }
  it.lastOpenAtMs = ms;
  emit dataChanged(this->index(index), this->index(index), {LastOpenAtMsRole});
  return true;
}

void ProfileListModel::clear() {
  beginResetModel();
  m_items.clear();
  endResetModel();
}
