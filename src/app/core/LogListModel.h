#pragma once

#include <QAbstractListModel>
#include <QStringList>

class LogListModel : public QAbstractListModel {
  Q_OBJECT

public:
  enum Roles {
    TextRole = Qt::UserRole + 1,
  };

  explicit LogListModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Q_INVOKABLE void clear();
  Q_INVOKABLE QString dump() const;
  void appendLine(const QString& line);

private:
  QStringList m_lines;
};
