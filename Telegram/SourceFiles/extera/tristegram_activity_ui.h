/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// How long I played, listened and coded: today, this week, all time.
void ShowActivityWindow(not_null<Window::SessionController*> controller);

// A capsule at the bottom of the chat list while I play or listen.
// It shows and hides itself; the parent sets its geometry and allows it
// with the "tristegramAllowed" property.
[[nodiscard]] object_ptr<Ui::RpWidget> CreateActivityChip(
	QWidget *parent,
	not_null<Window::SessionController*> controller);

} // namespace Tristegram
