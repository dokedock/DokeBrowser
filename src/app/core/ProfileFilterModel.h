#pragma once

#include <QSortFilterProxyModel>
#include <QSet>
#include <QStringList>

class ProfileFilterModel final : public QSortFilterProxyModel {
  Q_OBJECT

  Q_PROPERTY(QString groupFilter READ groupFilter WRITE setGroupFilter NOTIFY groupFilterChanged)
  Q_PROPERTY(QString keyword READ keyword WRITE setKeyword NOTIFY keywordChanged)
  Q_PROPERTY(bool onlyChecked READ onlyChecked WRITE setOnlyChecked NOTIFY onlyCheckedChanged)

public:
  explicit ProfileFilterModel(QObject* parent = nullptr);

  QString groupFilter() const;
  void setGroupFilter(const QString& value);

  QString keyword() const;
  void setKeyword(const QString& value);

  bool onlyChecked() const;
  void setOnlyChecked(bool value);

  QStringList checkedIds() const;
  void setCheckedIds(const QStringList& ids);

signals:
  void groupFilterChanged();
  void keywordChanged();
  void onlyCheckedChanged();
  void checkedIdsChanged();

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
  QString m_groupFilter = QStringLiteral("所有分组");
  QString m_keyword;
  bool m_onlyChecked = false;
  QSet<QString> m_checkedSet;
};
