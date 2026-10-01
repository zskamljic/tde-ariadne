#pragma once

#include "core/BatchRename.hpp"

#include <QStringList>

#include <optional>
#include <vector>

class QWidget;

namespace ariadne {

// Asks how to rename `paths` by find and replace, previewing every new name as it is
// typed. Returns the plans to apply, or nothing when cancelled.
std::optional<std::vector<batchrename::Plan>> askBatchRename(QWidget* parent, const QStringList& paths);

} // namespace ariadne
