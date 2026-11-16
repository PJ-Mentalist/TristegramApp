/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include <QtGui/QColor>
#include <QtGui/QPen>

#include <optional>

namespace Tristegram::Names {

// Sender name colors in chats, see "tristegram_name_mode":
// 0 Telegram colors, 1 accent, 2 own color, 3 logo gradient,
// 4 own gradient.
inline constexpr auto kModeCount = 5;

// The single color for every name, or the gradient start color.
[[nodiscard]] std::optional<QColor> Override(const QColor &accent);

// A pen with a horizontal gradient over [left, left + width) for the
// gradient modes, nothing otherwise.
[[nodiscard]] std::optional<QPen> GradientPen(int left, int width);

} // namespace Tristegram::Names
