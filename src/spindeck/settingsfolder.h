#pragma once

#include <QString>

namespace spindeck {

/// The folder Spin Deck keeps its settings, library and logs in:
/// ~/Library/Application Support/Spin Deck/ on a Mac.
/// The first time it is used, it copies the library and settings over from the folder
/// earlier builds used (the old Mixxx folder or the old sandbox folder). The old folder
/// is left as it is.
QString settingsFolder();

} // namespace spindeck
