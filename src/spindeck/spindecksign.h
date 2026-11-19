#pragma once

// Spin Deck by XTRAGOODLABS: a big sign that shows for a few seconds over the
// main window (for example "✓ GOOD MIX SAVED" or "STEMS READY").

#include <QString>

class QWidget;

namespace spindeck {

enum class SignStyle {
    Good,    // green border
    Neutral, // white border
    Working, // white border, stays until replaced or hidden
};

/// Show the sign. headline is big; detail is smaller and may have line breaks.
void showSign(const QString& headline,
        const QString& detail = QString(),
        SignStyle style = SignStyle::Neutral,
        QWidget* pWindow = nullptr);

/// Hide the sign now.
void hideSign();

} // namespace spindeck
