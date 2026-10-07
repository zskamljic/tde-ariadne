#pragma once

#include "core/FileChooser.hpp"

#include <QUrl>
#include <QWidget>

#include <vector>

class QComboBox;
class QLineEdit;
class QPushButton;

namespace ariadne {

class Application;
class DirectoryModel;
class FileSortProxy;
class FileView;
class PathBar;
class Sidebar;
struct FileEntry;

// The window that picks files for another program, as the FileChooser portal asks: to open
// one or more files or folders, to save one under a name, or to save several into a folder.
class FilePicker : public QWidget {
    Q_OBJECT

public:
    FilePicker(Application& app, chooser::Request request, QWidget* parent = nullptr);

signals:
    // Picked, or given up on with an empty answer; it closes itself after.
    void finished(bool picked, const chooser::Answer& answer);

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void navigate(const QUrl& url);
    void activate(const QModelIndex& index);
    void selectionChanged();
    void applyFilter();
    std::vector<FileEntry> selectedEntries() const;
    void accept();
    void finish(bool picked, QList<QUrl> urls = {});

    Application& m_app;
    chooser::Request m_request;
    QUrl m_location;
    bool m_done = false;

    DirectoryModel* m_model;
    FileSortProxy* m_proxy;
    FileView* m_view = nullptr;
    PathBar* m_pathBar = nullptr;
    Sidebar* m_sidebar = nullptr;
    QLineEdit* m_name = nullptr; // saving
    QComboBox* m_filter = nullptr;
    std::vector<QWidget*> m_choiceWidgets; // in the order of the request's choices
    QPushButton* m_accept = nullptr;
};

} // namespace ariadne
