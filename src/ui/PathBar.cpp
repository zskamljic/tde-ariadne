#include "PathBar.hpp"

#include "core/Location.hpp"

#include <tde/Theme.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QWindow>

#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

// Scrolls horizontally with the mouse wheel.
class CrumbScrollArea : public QScrollArea {
public:
    using QScrollArea::QScrollArea;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        const QPoint delta = event->angleDelta();
        const int amount = std::abs(delta.x()) > std::abs(delta.y()) ? delta.x() : delta.y();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - amount / 2);
        event->accept();
    }
};

// Reports clicks that land between or after the crumb buttons.
class CrumbContainer : public QWidget {
public:
    std::function<void()> onEmptyClicked;

    using QWidget::QWidget;

protected:
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && onEmptyClicked)
            onEmptyClicked();
    }
};

} // namespace

PathBar::PathBar(QWidget* parent)
    : QFrame(parent)
    , m_stack(new QStackedLayout(this))
    , m_scrollArea(new CrumbScrollArea(this))
    , m_crumbLayout(nullptr)
    , m_entry(new QLineEdit(this))
{
    setObjectName(u"PathBar"_s);
    setFixedHeight(34);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto container = std::make_unique<CrumbContainer>();
    container->setObjectName(u"CrumbContainer"_s);
    container->setCursor(Qt::IBeamCursor);
    container->onEmptyClicked = [this] { startEditing(); };
    container->installEventFilter(this);
    m_crumbContainer = container.get();
    m_crumbLayout = new QHBoxLayout(m_crumbContainer);
    m_crumbLayout->setContentsMargins(3, 0, 3, 0);
    m_crumbLayout->setSpacing(0);

    m_scrollArea->setObjectName(u"CrumbArea"_s);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setWidget(container.release()); // the scroll area owns it
    m_scrollArea->viewport()->setAutoFillBackground(false);

    m_entry->setObjectName(u"PathEntry"_s);
    m_entry->installEventFilter(this);

    m_stack->addWidget(m_scrollArea);
    m_stack->addWidget(m_entry);

    connect(m_entry, &QLineEdit::textEdited, this, &PathBar::complete);
    connect(m_entry, &QLineEdit::returnPressed, this, [this] { emit pathEntered(m_entry->text()); });
}

void PathBar::setLocation(const QUrl& url)
{
    m_location = url;
    if (url != m_deepest && !location::isAncestorOf(url, m_deepest))
        m_deepest = url;
    if (isEditing())
        stopEditing();
    rebuildCrumbs();
}

bool PathBar::isEditing() const
{
    return m_stack->currentWidget() == m_entry;
}

void PathBar::startEditing()
{
    m_entry->setText(location::editableText(m_location));
    m_previousText = m_entry->text();
    m_listedDirectory.clear();
    m_stack->setCurrentWidget(m_entry);
    setStateProperty("editing", true);
    m_entry->setFocus(Qt::ShortcutFocusReason);
    m_entry->selectAll();
}

void PathBar::stopEditing()
{
    m_stack->setCurrentWidget(m_scrollArea);
    setStateProperty("editing", false);
    setStateProperty("error", false);
}

void PathBar::showError()
{
    setStateProperty("error", true);
}

bool PathBar::moveWindowOnDrag(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        m_windowMoved = false;
        if (mouse->button() == Qt::LeftButton)
            m_pressPosition = mouse->globalPosition();
        return false;
    }
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (!m_pressPosition || !(mouse->buttons() & Qt::LeftButton)
            || (mouse->globalPosition() - *m_pressPosition).manhattanLength() < QApplication::startDragDistance())
            return false;
        m_pressPosition.reset();
        QWindow* handle = window()->windowHandle();
        if (!handle)
            return false;
        // Released, a button that is no longer down does not click.
        if (auto* button = qobject_cast<QAbstractButton*>(watched))
            button->setDown(false);
        m_windowMoved = true;
        handle->startSystemMove();
        return true;
    }
    case QEvent::MouseButtonRelease:
        m_pressPosition.reset();
        // The end of a move is not a click, should it arrive at all.
        return std::exchange(m_windowMoved, false);
    default:
        return false;
    }
}

bool PathBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_entry && moveWindowOnDrag(watched, event))
        return true;
    if (watched == m_entry) {
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Escape) {
                stopEditing();
                emit editingCancelled();
                return true;
            }
            if (key == Qt::Key_Tab) {
                acceptCompletion();
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut && isEditing()
            && static_cast<QFocusEvent*>(event)->reason() != Qt::PopupFocusReason) {
            stopEditing();
        }
    }
    return QFrame::eventFilter(watched, event);
}

void PathBar::rebuildCrumbs()
{
    // Crumbs are replaced while one of them may still be delivering its clicked() signal.
    while (QLayoutItem* item = m_crumbLayout->takeAt(0)) {
        const std::unique_ptr<QLayoutItem> taken(item);
        if (QWidget* widget = taken->widget()) {
            widget->hide();
            widget->deleteLater();
        }
    }

    const auto crumbs = location::crumbs(m_deepest);
    m_currentCrumb = nullptr;
    for (std::size_t i = 0; i < crumbs.size(); ++i) {
        const location::Crumb& crumb = crumbs[i];
        if (i > 0) {
            auto* separator = new QLabel(u"/"_s, m_crumbContainer);
            separator->setObjectName(u"CrumbSeparator"_s);
            separator->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_crumbLayout->addWidget(separator);
        }

        auto* button = new QToolButton(m_crumbContainer);
        button->setObjectName(u"Crumb"_s);
        button->setCursor(Qt::ArrowCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setText(crumb.label);
        button->setToolTip(location::editableText(crumb.url));
        if (!crumb.iconName.isEmpty()) {
            button->setIcon(tde::theme::symbolicIcon(crumb.iconName));
            button->setIconSize(QSize(16, 16));
        }
        button->setToolButtonStyle(crumb.label.isEmpty() ? Qt::ToolButtonIconOnly
                : crumb.iconName.isEmpty()               ? Qt::ToolButtonTextOnly
                                                         : Qt::ToolButtonTextBesideIcon);
        const bool current = crumb.url == m_location;
        button->setProperty("current", current);
        if (current)
            m_currentCrumb = button;
        connect(button, &QToolButton::clicked, this, [this, url = crumb.url] { emit locationClicked(url); });
        button->installEventFilter(this);
        m_crumbLayout->addWidget(button);
    }
    m_crumbLayout->addStretch(1);

    QTimer::singleShot(0, this, &PathBar::scrollToCurrent);
}

void PathBar::scrollToCurrent()
{
    QScrollBar* bar = m_scrollArea->horizontalScrollBar();
    if (m_location == m_deepest)
        bar->setValue(bar->maximum());
    else if (m_currentCrumb)
        m_scrollArea->ensureWidgetVisible(m_currentCrumb, 60, 0);
}

void PathBar::complete(const QString& text)
{
    setStateProperty("error", false);

    // Only complete while typing forward at the end; deleting must not bring the rest back.
    const QString previous = std::exchange(m_previousText, text);
    if (text.size() <= previous.size() || m_entry->cursorPosition() != text.size())
        return;

    const qsizetype slash = text.lastIndexOf(u'/');
    const QString typedDirectory = text.left(slash + 1);
    const QString prefix = text.mid(slash + 1);
    if (prefix.isEmpty())
        return;
    const auto directory
        = typedDirectory.isEmpty() ? std::optional(m_location) : location::fromUserInput(typedDirectory, m_location);
    if (!directory)
        return;

    QStringList matches;
    for (const QString& name : subdirectories(location::localPath(*directory))) {
        if (name.startsWith(prefix, Qt::CaseInsensitive) && (prefix.startsWith(u'.') || !name.startsWith(u'.')))
            matches << name;
    }
    if (matches.isEmpty())
        return;

    // Complete as far as all matches agree, in the spelling of the folder on disk.
    QString common = matches.first();
    for (const QString& match : std::as_const(matches)) {
        qsizetype length = 0;
        while (length < common.size() && length < match.size()
            && common[length].toCaseFolded() == match[length].toCaseFolded())
            ++length;
        common.truncate(length);
    }
    if (common.size() <= prefix.size())
        return;

    const QString completed = typedDirectory + common;
    m_entry->setText(completed);
    m_entry->setSelection(text.size(), completed.size() - text.size());
}

void PathBar::acceptCompletion()
{
    m_entry->deselect();
    m_entry->end(false);
    QString text = m_entry->text();
    const auto url = location::fromUserInput(text, m_location);
    if (!text.endsWith(u'/') && url && QFileInfo(location::localPath(*url)).isDir())
        m_entry->setText(text += u'/');
    m_previousText = text;
}

const QStringList& PathBar::subdirectories(const QString& directory)
{
    if (directory != m_listedDirectory) {
        m_listedDirectory = directory;
        m_directoryNames = QDir(directory).entryList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name | QDir::IgnoreCase);
    }
    return m_directoryNames;
}

void PathBar::setStateProperty(const char* name, bool value)
{
    if (property(name).toBool() == value)
        return;
    setProperty(name, value);
    style()->unpolish(this);
    style()->polish(this);
}

} // namespace ariadne
