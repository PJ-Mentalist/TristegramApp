/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class QMenu;

namespace Ui {
class InputField;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Tristegram {

// "AI editor" in the context menu of the message field: Gemini fixes the
// mistakes, rewrites in another tone, translates or follows any request.
// Works on the selected part or on the whole text.
void FillAiWriterMenu(
	not_null<QMenu*> menu,
	not_null<Ui::InputField*> field,
	not_null<Window::SessionController*> controller);

// The key, the model, the personal style and how the result is applied.
void ShowAiWriterSettings(not_null<Window::SessionController*> controller);

} // namespace Tristegram
