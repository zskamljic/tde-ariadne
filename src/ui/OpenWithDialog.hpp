#pragma once

#include <QString>

#include <optional>

class QWidget;

namespace ariadne {

class Applications;

struct OpenWithChoice {
    QString appId;
    bool makeDefault = false;
};

// Lets the user pick any installed application for a file, recommended ones first, and
// optionally make it the default for the file's type.
std::optional<OpenWithChoice> chooseApplication(QWidget* parent, const Applications& applications,
    const QString& mimeType, const QString& typeDescription, const QString& fileName);

} // namespace ariadne
