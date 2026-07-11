#include "ProxyPoolModel.h"

ProxyPoolModel::ProxyPoolModel(QObject* parent) : QAbstractListModel(parent) {}

int ProxyPoolModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_items.size();
}

QVariant ProxyPoolModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
    return {};
  }
  const auto& it = m_items.at(index.row());
  switch (role) {
    case ProxyIdRole:
      return it.id;
    case ProxyTypeRole:
      return it.type;
    case ProxyHostRole:
      return it.host;
    case ProxyPortRole:
      return it.port;
    case ProxyUsernameRole:
      return it.username;
    case ProxyPasswordRole:
      return it.password;
    case ProxyRemarkRole:
      return it.remark;
    case ProxyDisabledRole:
      return it.disabled;
    case ProxyCreatedAtMsRole:
      return it.createdAtMs;
    case ProxyLastOkRole:
      return it.lastOk;
    case ProxyLastIpRole:
      return it.lastIp;
    case ProxyLastErrorRole:
      return it.lastError;
    case ProxyLastAtMsRole:
      return it.lastAtMs;
    case ProxyAssignedProfileIdRole:
      return it.assignedProfileId;
    default:
      return {};
  }
}

QHash<int, QByteArray> ProxyPoolModel::roleNames() const {
  QHash<int, QByteArray> r;
  r.insert(ProxyIdRole, "proxyId");
  r.insert(ProxyTypeRole, "proxyType");
  r.insert(ProxyHostRole, "proxyHost");
  r.insert(ProxyPortRole, "proxyPort");
  r.insert(ProxyUsernameRole, "proxyUsername");
  r.insert(ProxyPasswordRole, "proxyPassword");
  r.insert(ProxyRemarkRole, "proxyRemark");
  r.insert(ProxyDisabledRole, "proxyDisabled");
  r.insert(ProxyCreatedAtMsRole, "proxyCreatedAtMs");
  r.insert(ProxyLastOkRole, "proxyLastOk");
  r.insert(ProxyLastIpRole, "proxyLastIp");
  r.insert(ProxyLastErrorRole, "proxyLastError");
  r.insert(ProxyLastAtMsRole, "proxyLastAtMs");
  r.insert(ProxyAssignedProfileIdRole, "proxyAssignedProfileId");
  return r;
}

const QVector<ProxyPoolModel::ProxyItem>& ProxyPoolModel::items() const {
  return m_items;
}

void ProxyPoolModel::setItems(const QVector<ProxyItem>& items) {
  beginResetModel();
  m_items = items;
  endResetModel();
}

