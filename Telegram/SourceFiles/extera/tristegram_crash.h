/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class QString;

namespace Tristegram {

// On a crash writes tristegram_crash.txt next to the app: the error code
// and the call stack as module offsets, readable with Telegram.map.
void InstallCrashLog();

// Remembers what was clicked and pressed last, for the crash report.
void InstallCrashBreadcrumbs();

// The log of the previous crash if there was one since the last check,
// moved aside so it is reported only once.
[[nodiscard]] QString TakeLastCrashLog();

} // namespace Tristegram
