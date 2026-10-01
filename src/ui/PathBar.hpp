#pragma once

#include <QFrame>
#include <QPointF>
#include <QPointer>
#include <QUrl>

#include <optional>

class QHBoxLayout;
class QLineEdit;
class QScrollArea;
class QStackedLayout;
class QToolButton;

namespace ariadne {

// Breadcrumb buttons for the current location. Clicking empty space turns the bar into a
// text entry that completes folder names as you type; dragging moves the window. Crumbs deeper than the current
// location stay visible after going up, so you can go back down with one click.
class PathBar : public QFrame {
    Q_OBJECT

public:
    explicit PathBar(QWidget* parent = nullptr);

    void setLocation(const QUrl& url);
    void startEditing();
    void stopEditing();
    bool isEditing() const;
    void showError();

signals:
    void locationClicked(const QUrl& url);
    void pathEntered(const QString& text);
    void editingCancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildCrumbs();
    void scrollToCurrent();
    // Inline completion of folder names, like a shell: the completed part stays selected, so
    // typing on replaces it and Tab accepts it.
    void complete(const QString& text);
    void acceptCompletion();
    const QStringList& subdirectories(const QString& directory);
    void setStateProperty(const char* name, bool value);
    // Dragging the crumbs or the space around them moves the window; clicks still click.
    bool moveWindowOnDrag(QObject* watched, QEvent* event);

    QStackedLayout* m_stack;
    QScrollArea* m_scrollArea;
    QWidget* m_crumbContainer;
    QHBoxLayout* m_crumbLayout;
    QLineEdit* m_entry;
    QString m_previousText;
    QString m_listedDirectory;
    QStringList m_directoryNames;
    QPointer<QToolButton> m_currentCrumb;
    QUrl m_location;
    QUrl m_deepest;
    std::optional<QPointF> m_pressPosition;
    bool m_windowMoved = false;
};

} // namespace ariadne
