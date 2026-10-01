#pragma once

#include <QCollator>
#include <QLocale>

namespace ariadne {

// Compares names the way people read them: "file2" before "file10", ignoring case, in the
// order of the user's language. Under the C locale (no LANG, as with some services and
// containers) Qt's collator compares code points and ignores numbers, so English rules are
// used there instead.
inline QCollator naturalCollator()
{
    QLocale locale;
    if (locale.language() == QLocale::C)
        locale = QLocale(QLocale::English, QLocale::UnitedStates);
    QCollator collator(locale);
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    return collator;
}

} // namespace ariadne
