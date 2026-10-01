#include "BatchRenameDialog.hpp"

#include <tde/Dialog.hpp>
#include <tde/Theme.hpp>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace ariadne {

std::optional<std::vector<batchrename::Plan>> askBatchRename(QWidget* parent, const QStringList& paths)
{
    tde::Dialog dialog(u"Rename %1 Items"_s.arg(paths.size()), parent);

    auto* find = new QLineEdit;
    find->setPlaceholderText(u"Text to find"_s);
    auto* replace = new QLineEdit;
    replace->setPlaceholderText(u"Replace it with"_s);
    auto* regex = new QCheckBox(u"Regular expression"_s);
    regex->setToolTip(u"Use \\1, \\2, … in the replacement for the parts in parentheses."_s);
    auto* matchCase = new QCheckBox(u"Match case"_s);
    auto* extensions = new QCheckBox(u"Include extensions"_s);

    auto* preview = new QTreeWidget;
    preview->setObjectName(u"ListView"_s);
    preview->setColumnCount(2);
    preview->setHeaderLabels({u"Name"_s, u"New name"_s});
    preview->setRootIsDecorated(false);
    preview->setUniformRowHeights(true);
    preview->setSelectionMode(QAbstractItemView::NoSelection);
    preview->setFocusPolicy(Qt::NoFocus);
    preview->header()->setSectionResizeMode(QHeaderView::Stretch);
    preview->setMinimumSize(560, 280);

    auto* summary = new QLabel;
    summary->setObjectName(u"AboutDetails"_s);
    summary->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* rename = buttons->addButton(u"Rename"_s, QDialogButtonBox::AcceptRole);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    std::vector<batchrename::Plan> plans;
    const auto update = [&] {
        const batchrename::Options options {
            find->text(), replace->text(), regex->isChecked(), matchCase->isChecked(), extensions->isChecked()};
        const auto planned = batchrename::plan(paths, options);
        preview->clear();
        if (!planned) {
            plans.clear();
            summary->setText(u"The expression is not valid: %1."_s.arg(planned.error()));
            rename->setEnabled(false);
            return;
        }
        plans = *planned;

        int changing = 0;
        int problems = 0;
        const QColor dim = tde::theme::colors().dimText;
        const QColor error = tde::theme::colors().error;
        for (const batchrename::Plan& plan : plans) {
            auto* row = new QTreeWidgetItem(preview, {plan.oldName, plan.changes() ? plan.newName : u"(unchanged)"_s});
            if (!plan.changes()) {
                row->setForeground(1, dim);
            } else if (!plan.problem.isEmpty()) {
                row->setForeground(1, error);
                row->setToolTip(1, plan.problem);
                ++problems;
            } else {
                ++changing;
            }
        }
        if (problems > 0)
            summary->setText(problems == 1 ? u"1 name has a problem; hover it to see why."_s
                                           : u"%1 names have problems; hover them to see why."_s.arg(problems));
        else if (changing == 0)
            summary->setText(u"Nothing would be renamed yet."_s);
        else
            summary->setText(u"%1 of %2 items will be renamed."_s.arg(changing).arg(plans.size()));
        rename->setEnabled(changing > 0 && problems == 0);
    };
    for (QLineEdit* field : {find, replace})
        QObject::connect(field, &QLineEdit::textChanged, &dialog, update);
    for (QCheckBox* box : {regex, matchCase, extensions})
        QObject::connect(box, &QCheckBox::toggled, &dialog, update);

    auto* fields = new QFormLayout;
    fields->addRow(u"Find"_s, find);
    fields->addRow(u"Replace with"_s, replace);
    auto* options = new QHBoxLayout;
    options->addWidget(regex);
    options->addWidget(matchCase);
    options->addWidget(extensions);
    options->addStretch(1);

    QVBoxLayout* layout = dialog.contentLayout();
    layout->addLayout(fields);
    layout->addLayout(options);
    layout->addWidget(preview);
    layout->addWidget(summary);
    layout->addWidget(buttons);
    dialog.setDefaultButton(rename);
    update();
    find->setFocus();

    if (dialog.run() != QDialog::Accepted)
        return std::nullopt;
    return plans;
}

} // namespace ariadne
