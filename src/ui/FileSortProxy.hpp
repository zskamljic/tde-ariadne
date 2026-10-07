#pragma once

#include "core/Config.hpp"
#include "core/DirectoryListing.hpp"

#include <QCollator>
#include <QSortFilterProxyModel>

#include <functional>

namespace ariadne {

class DirectoryModel;

// Natural-order sorting with folders first, and hiding of hidden files.
class FileSortProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit FileSortProxy(QObject* parent = nullptr);

    void setDirectoryModel(DirectoryModel* model);

    SortKey sortKey() const { return m_key; }
    bool isDescending() const { return m_descending; }
    bool showHidden() const { return m_showHidden; }
    bool foldersFirst() const { return m_foldersFirst; }
    bool isCaseSensitive() const { return m_caseSensitive; }

    void setSort(SortKey key, bool descending);
    void setFoldersFirst(bool foldersFirst);
    void setShowHidden(bool show);
    // Case-sensitive sorting puts uppercase before lowercase, like `ls`.
    void setCaseSensitive(bool caseSensitive);
    // Only files `accepts` passes are shown, as when picking files of some kind; folders
    // always are, to get around. None shows every file.
    void setFileFilter(std::function<bool(const FileEntry&)> accepts);

    // Every folder keeps its arrow, also while it is being listed and when all it holds is hidden.
    bool hasChildren(const QModelIndex& parent = {}) const override;

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    int compareNames(const QString& a, const QString& b) const;

    DirectoryModel* m_model = nullptr;
    QCollator m_collator;
    SortKey m_key = SortKey::Name;
    bool m_descending = false;
    bool m_foldersFirst = true;
    bool m_showHidden = false;
    bool m_caseSensitive = false;
    std::function<bool(const FileEntry&)> m_fileFilter;
};

} // namespace ariadne
