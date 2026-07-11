#include "ProfileFilterModel.h"

#include "ProfileListModel.h"

ProfileFilterModel::ProfileFilterModel(QObject* parent) : QSortFilterProxyModel(parent) {
  setFilterCaseSensitivity(Qt::CaseInsensitive);
  setSortCaseSensitivity(Qt::CaseInsensitive);
}

QString ProfileFilterModel::groupFilter() const {
  return m_groupFilter;
}

void ProfileFilterModel::setGroupFilter(const QString& value) {
  const QString v = value.trimmed();
  const QString next = v.isEmpty() ? QStringLiteral("所有分组") : v;
  if (m_groupFilter == next) {
    return;
  }
  m_groupFilter = next;
  beginFilterChange();
  endFilterChange();
  emit groupFilterChanged();
}

QString ProfileFilterModel::keyword() const {
  return m_keyword;
}

void ProfileFilterModel::setKeyword(const QString& value) {
  const QString v = value.trimmed();
  if (m_keyword == v) {
    return;
  }
  m_keyword = v;
  beginFilterChange();
  endFilterChange();
  emit keywordChanged();
}

bool ProfileFilterModel::onlyChecked() const {
  return m_onlyChecked;
}

void ProfileFilterModel::setOnlyChecked(bool value) {
  if (m_onlyChecked == value) {
    return;
  }
  m_onlyChecked = value;
  beginFilterChange();
  endFilterChange();
  emit onlyCheckedChanged();
}

QStringList ProfileFilterModel::checkedIds() const {
  return m_checkedSet.values();
}

void ProfileFilterModel::setCheckedIds(const QStringList& ids) {
  QSet<QString> set;
  for (const auto& id : ids) {
    const QString v = id.trimmed();
    if (!v.isEmpty()) {
      set.insert(v);
    }
  }
  if (set == m_checkedSet) {
    return;
  }
  m_checkedSet = set;
  beginFilterChange();
  endFilterChange();
  emit checkedIdsChanged();
}

bool ProfileFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
  const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
  const QString id = sourceModel()->data(idx, ProfileListModel::IdRole).toString();
  const QString name = sourceModel()->data(idx, ProfileListModel::NameRole).toString();
  const QString group = sourceModel()->data(idx, ProfileListModel::GroupRole).toString();
  const QString remark = sourceModel()->data(idx, ProfileListModel::RemarkRole).toString();

  if (id.isEmpty()) {
    return false;
  }

  if (m_onlyChecked) {
    if (!m_checkedSet.contains(id)) {
      return false;
    }
  }

  if (!m_groupFilter.isEmpty() && m_groupFilter != QStringLiteral("所有分组")) {
    if (group != m_groupFilter) {
      return false;
    }
  }

  if (!m_keyword.isEmpty()) {
    const QString k = m_keyword;
    const bool hit = name.contains(k, Qt::CaseInsensitive) || group.contains(k, Qt::CaseInsensitive) ||
                     remark.contains(k, Qt::CaseInsensitive) || id.contains(k, Qt::CaseInsensitive);
    if (!hit) {
      return false;
    }
  }

  return true;
}
