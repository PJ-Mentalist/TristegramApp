/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_templates.h"

#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QPointer>
#include <QtGui/QTextCursor>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>

namespace Tristegram {
namespace {

constexpr auto kListKey = "tristegram_templates_list";
constexpr auto kMaxShown = 24;
constexpr auto kMaxTitle = 48;

[[nodiscard]] QString ListKey() {
	return QString::fromLatin1(kListKey);
}

} // namespace

bool TemplatesEnabled() {
	return Extera::Bool(u"tristegram_templates"_q);
}

QStringList Templates() {
	auto result = QStringList();
	const auto all = Extera::String(ListKey()).split(QChar('\n'));
	for (const auto &line : all) {
		const auto trimmed = line.trimmed();
		if (!trimmed.isEmpty()) {
			result.push_back(trimmed);
		}
	}
	return result;
}

void FillTemplatesMenu(
		not_null<QMenu*> menu,
		not_null<Ui::InputField*> field,
		Fn<void()> edit) {
	const auto list = Templates();
	menu->addSeparator();
	const auto sub = menu->addMenu(tr::lng_tgs_templates_menu(tr::now));
	const auto weak = QPointer<Ui::InputField>(field.get());
	auto shown = 0;
	for (const auto &phrase : list) {
		if (++shown > kMaxShown) {
			break;
		}
		const auto title = (phrase.size() > kMaxTitle)
			? (phrase.mid(0, kMaxTitle - 1) + QChar(0x2026))
			: phrase;
		sub->addAction(title, [=] {
			if (const auto strong = weak.data()) {
				const auto text = strong->rawTextEdit();
				auto cursor = text->textCursor();
				cursor.insertText(phrase);
				text->setTextCursor(cursor);
				strong->setFocus();
			}
		});
	}
	if (!list.isEmpty()) {
		sub->addSeparator();
	}
	sub->addAction(tr::lng_tgs_templates_edit(tr::now), [=] {
		if (edit) {
			edit();
		}
	});
}

void EditTemplatesBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(tr::lng_tgs_templates_title());
	box->setWidth(st::boxWideWidth);
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		tr::lng_tgs_templates_placeholder(),
		Extera::String(ListKey())));
	field->setMaxHeight(st::defaultInputField.heightMax * 3);
	box->setFocusCallback([=] { field->setFocusFast(); });
	box->addButton(tr::lng_settings_save(), [=] {
		Extera::Config::Instance().setString(
			ListKey(),
			field->getLastText().trimmed());
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace Tristegram
