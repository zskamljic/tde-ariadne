#include "OpenWithDialog.hpp"

#include "core/Applications.hpp"

#include <tde/Dialog.hpp>
#include <tde/Theme.hpp>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace ariadne {
namespace {

constexpr int AppIdRole = Qt::UserRole + 1;

QListWidgetItem* addHeading(QListWidget* list, const QString& text)
{
    auto* item = new QListWidgetItem(text, list);
    item->setFlags(Qt::NoItemFlags);
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
    return item;
}

void addApp(QListWidget* list, const DesktopApp* app)
{
    // Scale small icons up, so every row lines up the same.
    const QIcon icon = tde::theme::themeIcon({app->iconName, u"application-x-executable"_s});
    const qreal scale = list->devicePixelRatioF();
    const QSize size = list->iconSize() * scale;
    QPixmap pixmap = icon.pixmap(list->iconSize(), scale);
    if (!pixmap.isNull() && pixmap.size() != size) {
        pixmap = pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(scale);
    }
    auto* item = new QListWidgetItem(QIcon(pixmap), app->name, list);
    item->setData(AppIdRole, app->id);
    item->setToolTip(app->exec);
}

} // namespace

std::optional<OpenWithChoice> chooseApplication(QWidget* parent, const Applications& applications,
    const QString& mimeType, const QString& typeDescription, const QString& fileName)
{
    tde::Dialog dialog(u"Open With"_s, parent);
    auto* prompt = new QLabel(u"Choose an application to open “%1”."_s.arg(fileName));
    prompt->setWordWrap(true);
    auto* search = new QLineEdit;
    search->setPlaceholderText(u"Search applications"_s);
    search->setClearButtonEnabled(true);
    auto* list = new QListWidget;
    list->setObjectName(u"AppList"_s);
    list->setIconSize(QSize(24, 24));
    list->setMinimumSize(380, 340);
    auto* makeDefault = new QCheckBox(u"Always use for %1 files"_s.arg(typeDescription));

    const QList<const DesktopApp*> recommended = applications.forMimeType(mimeType);
    if (!recommended.isEmpty()) {
        addHeading(list, u"Recommended"_s);
        for (const DesktopApp* app : recommended)
            addApp(list, app);
        addHeading(list, u"Other Applications"_s);
    }
    for (const DesktopApp* app : applications.visible()) {
        if (!recommended.contains(app))
            addApp(list, app);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* open = buttons->addButton(u"Open"_s, QDialogButtonBox::AcceptRole);
    open->setEnabled(false);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog,
        [open](QListWidgetItem* item) { open->setEnabled(item && item->data(AppIdRole).isValid()); });
    QObject::connect(list, &QListWidget::itemActivated, &dialog, [&dialog](QListWidgetItem* item) {
        if (item->data(AppIdRole).isValid())
            dialog.accept();
    });
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [list](const QString& text) {
        // Headings only make sense for the full list.
        for (int row = 0; row < list->count(); ++row) {
            QListWidgetItem* item = list->item(row);
            const bool heading = !item->data(AppIdRole).isValid();
            item->setHidden(heading ? !text.isEmpty() : !item->text().contains(text, Qt::CaseInsensitive));
        }
    });

    QVBoxLayout* layout = dialog.contentLayout();
    layout->addWidget(prompt);
    layout->addWidget(search);
    layout->addWidget(list);
    layout->addWidget(makeDefault);
    layout->addWidget(buttons);
    dialog.setDefaultButton(open);
    search->setFocus();

    if (dialog.run() != QDialog::Accepted || !list->currentItem())
        return std::nullopt;
    return OpenWithChoice {list->currentItem()->data(AppIdRole).toString(), makeDefault->isChecked()};
}

} // namespace ariadne
