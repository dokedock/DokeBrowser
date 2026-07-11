#include "LogListModel.h"

LogListModel::LogListModel(QObject* parent) : QAbstractListModel(parent) {}

int LogListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }
  return m_lines.size();
}

QVariant LogListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size()) {
    return {};
  }
  if (role == TextRole || role == Qt::DisplayRole) {
    return m_lines.at(index.row());
  }
  return {};
}

QHash<int, QByteArray> LogListModel::roleNames() const {
  return {
      {TextRole, "text"},
  };
}

void LogListModel::clear() {
  beginResetModel();
  m_lines.clear();
  endResetModel();
}

void LogListModel::appendLine(const QString& line) {
  const int row = m_lines.size();
  beginInsertRows(QModelIndex(), row, row);
  m_lines.push_back(line);
  endInsertRows();
}

