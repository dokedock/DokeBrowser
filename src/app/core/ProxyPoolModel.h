#pragma once

#include <QAbstractListModel>
#include <QVector>

#include "ProfileRepository.h"

class ProxyPoolModel final : public QAbstractListModel {
  Q_OBJECT

public:
  using ProxyItem = ProfileRepository::ProxyPoolItem;

  enum Roles {
    ProxyIdRole = Qt::UserRole + 1,
    ProxyTypeRole,
    ProxyHostRole,
    ProxyPortRole,
    ProxyUsernameRole,
    ProxyPasswordRole,
    ProxyRemarkRole,
    ProxyDisabledRole,
    ProxyCreatedAtMsRole,
    ProxyLastOkRole,
    ProxyLastIpRole,
    ProxyLastErrorRole,
    ProxyLastAtMsRole,
    ProxyAssignedProfileIdRole,
  };

  explicit ProxyPoolModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  const QVector<ProxyItem>& items() const;
  void setItems(const QVector<ProxyItem>& items);

private:
  QVector<ProxyItem> m_items;
};

