/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

namespace rpl {
class lifetime;
} // namespace rpl

namespace Tristegram::Icons {

// Areas with their own icon look, see the "Icons" settings section.
enum class Area {
	Menu,
	Compose,
	Folders,
	Buttons,
};
inline constexpr auto kAreaCount = 4;

// Modes: 0 theme, 1 accent, 2 own color, 3 logo gradient, 4 own gradient.
inline constexpr auto kModeCount = 5;

[[nodiscard]] QString GlobalKey(const QString &suffix);
[[nodiscard]] QString AreaKey(Area area, const QString &suffix);

void Init(rpl::lifetime &lifetime);

} // namespace Tristegram::Icons
