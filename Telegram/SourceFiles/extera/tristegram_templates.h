/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#pragma once

class QMenu;

namespace Ui {
class GenericBox;
class InputField;
} // namespace Ui

namespace Tristegram {

// Saved phrases, one per line in tristegram_templates_list.
[[nodiscard]] bool TemplatesEnabled();
[[nodiscard]] QStringList Templates();

// Adds a "Templates" submenu to the message field context menu.
void FillTemplatesMenu(
	not_null<QMenu*> menu,
	not_null<Ui::InputField*> field,
	Fn<void()> edit);

void EditTemplatesBox(not_null<Ui::GenericBox*> box);

} // namespace Tristegram
