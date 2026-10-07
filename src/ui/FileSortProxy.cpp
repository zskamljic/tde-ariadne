#include "FileSortProxy.hpp"

#include "DirectoryModel.hpp"
#include "core/Collation.hpp"

namespace ariadne {
namespace {

template <typename T> int compare(const T& a, const T& b)
{
    return a < b ? -1 : (b < a ? 1 : 0);
}

// Code point order (so uppercase before lowercase), with runs of digits compared by value.
int naturalCompare(QStringView a, QStringView b)
{
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype endA = i;
            qsizetype endB = j;
            while (endA < a.size() && a[endA].isDigit())
                ++endA;
            while (endB < b.size() && b[endB].isDigit())
                ++endB;
            QStringView numberA = a.sliced(i, endA - i);
            QStringView numberB = b.sliced(j, endB - j);
            while (numberA.size() > 1 && numberA.front() == u'0')
                numberA = numberA.sliced(1);
            while (numberB.size() > 1 && numberB.front() == u'0')
                numberB = numberB.sliced(1);
            if (numberA.size() != numberB.size())
                return numberA.size() < numberB.size() ? -1 : 1;
            if (const int result = numberA.compare(numberB); result != 0)
                return result < 0 ? -1 : 1;
            i = endA;
            j = endB;
            continue;
        }
        if (a[i] != b[j])
            return a[i] < b[j] ? -1 : 1;
        ++i;
        ++j;
    }
    return compare(a.size() - i, b.size() - j);
}

} // namespace

FileSortProxy::FileSortProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
    , m_collator(naturalCollator())
{
    setDynamicSortFilter(true);
}

void FileSortProxy::setDirectoryModel(DirectoryModel* model)
{
    m_model = model;
    setSourceModel(model);
    // Direction is handled in lessThan(), so that "folders first" holds both ways.
    sort(0, Qt::AscendingOrder);
}

void FileSortProxy::setSort(SortKey key, bool descending)
{
    if (key == m_key && descending == m_descending)
        return;
    m_key = key;
    m_descending = descending;
    invalidate();
}

void FileSortProxy::setFoldersFirst(bool foldersFirst)
{
    if (foldersFirst == m_foldersFirst)
        return;
    m_foldersFirst = foldersFirst;
    invalidate();
}

void FileSortProxy::setCaseSensitive(bool caseSensitive)
{
    if (caseSensitive == m_caseSensitive)
        return;
    m_caseSensitive = caseSensitive;
    invalidate();
}

int FileSortProxy::compareNames(const QString& a, const QString& b) const
{
    if (m_caseSensitive)
        return naturalCompare(a, b);
    // Folding first makes this independent of how the collation backend treats case.
    return m_collator.compare(a.toCaseFolded(), b.toCaseFolded());
}

void FileSortProxy::setShowHidden(bool show)
{
    if (show == m_showHidden)
        return;
    m_showHidden = show;
    invalidate();
}

bool FileSortProxy::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    const FileEntry& a = m_model->entry(left);
    const FileEntry& b = m_model->entry(right);

    if (m_foldersFirst && a.isDir != b.isDir)
        return a.isDir;

    int result = 0;
    switch (m_key) {
    case SortKey::Name:
        break;
    case SortKey::Modified:
        result = compare(a.modified, b.modified);
        break;
    case SortKey::Size:
        result = a.isDir && b.isDir ? compare(a.childCount, b.childCount) : compare(a.size, b.size);
        break;
    case SortKey::Type:
        result = m_collator.compare(a.mimeComment, b.mimeComment);
        break;
    }
    if (result == 0)
        result = compareNames(a.name, b.name);
    if (result == 0)
        result = compare(a.name, b.name);
    return m_descending ? result > 0 : result < 0;
}

bool FileSortProxy::hasChildren(const QModelIndex& parent) const
{
    return parent.isValid() ? m_model->hasChildren(mapToSource(parent)) : QSortFilterProxyModel::hasChildren(parent);
}

bool FileSortProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const FileEntry& entry = m_model->entry(m_model->index(sourceRow, 0, sourceParent));
    if (entry.isHidden && !m_showHidden)
        return false;
    return entry.isDir || !m_fileFilter || m_fileFilter(entry);
}

void FileSortProxy::setFileFilter(std::function<bool(const FileEntry&)> accepts)
{
    m_fileFilter = std::move(accepts);
    invalidate();
}

} // namespace ariadne
