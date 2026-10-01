#pragma once

#include "core/DirectoryListing.hpp"

#include <QIcon>

#include <vector>

class QWidget;

namespace ariadne {

// Opens a window with details about `entries`: type, size, location and dates, and the
// permissions of a single item, which its owner can change there.
void showProperties(QWidget* parent, const std::vector<FileEntry>& entries, const QIcon& icon);

} // namespace ariadne
