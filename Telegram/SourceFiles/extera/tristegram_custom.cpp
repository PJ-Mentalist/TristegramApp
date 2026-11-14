/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_custom.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_wall_paper.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "storage/localstorage.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/color_editor.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

namespace Tristegram {
namespace {

using Window::Theme::EmbeddedType;

struct ThemePreset {
	EmbeddedType type;
	QColor accent;
};

[[nodiscard]] const std::vector<ThemePreset> &ThemePresets() {
	static const auto result = std::vector<ThemePreset>{
		{ EmbeddedType::Night, QColor(0xF0, 0x7A, 0x0C) },
		{ EmbeddedType::Night, QColor(0xFF, 0x45, 0x3A) },
		{ EmbeddedType::Night, QColor(0x0A, 0x84, 0xFF) },
		{ EmbeddedType::Default, QColor(0xFF, 0x37, 0x5F) },
		{ EmbeddedType::Night, QColor(0xBF, 0x5A, 0xF2) },
		{ EmbeddedType::NightGreen, QColor(0x30, 0xD1, 0x58) },
		{ EmbeddedType::Night, QColor(0x8E, 0x8E, 0x93) },
	};
	return result;
}

[[nodiscard]] const std::vector<std::vector<QColor>> &BackgroundPresets() {
	static const auto result = std::vector<std::vector<QColor>>{
		{ QColor(0xF0, 0x7A, 0x0C), QColor(0xB3, 0x23, 0x0A), QColor(0x1A, 0x0A, 0x05), QColor(0x5C, 0x1A, 0x05) },
		{ QColor(0xFF, 0x5F, 0x6D), QColor(0xFF, 0xC3, 0x71), QColor(0x8E, 0x2D, 0xE2), QColor(0xFF, 0x9A, 0x5A) },
		{ QColor(0x0F, 0x20, 0x27), QColor(0x2C, 0x53, 0x64), QColor(0x00, 0xC6, 0xFF), QColor(0x1B, 0x3A, 0x4B) },
		{ QColor(0x00, 0xF5, 0xA0), QColor(0x00, 0xD9, 0xF5), QColor(0x7B, 0x2F, 0xF7), QColor(0x1E, 0x3C, 0x72) },
		{ QColor(0xFC, 0x00, 0xFF), QColor(0x00, 0xDB, 0xDE), QColor(0x3A, 0x1C, 0x71), QColor(0x8E, 0x2D, 0xE2) },
		{ QColor(0x0B, 0x3D, 0x2E), QColor(0x71, 0xB2, 0x80), QColor(0x13, 0x4E, 0x5E), QColor(0x2E, 0x6B, 0x4F) },
		{ QColor(0xFF, 0x9A, 0x9E), QColor(0xFA, 0xD0, 0xC4), QColor(0xA1, 0x8C, 0xD1), QColor(0xFB, 0xC2, 0xEB) },
		{ QColor(0x0F, 0x0F, 0x12), QColor(0x41, 0x43, 0x45), QColor(0x23, 0x25, 0x26), QColor(0x2C, 0x2C, 0x30) },
	};
	return result;
}

[[nodiscard]] QString Hex(const QColor &color) {
	return color.name(QColor::HexRgb).mid(1).toLower();
}

} // namespace

std::vector<QString> ThemePresetNames() {
	return {
		u"Tristegram"_q,
		tr::lng_tristegram_theme_embers(tr::now),
		tr::lng_tristegram_theme_ocean(tr::now),
		tr::lng_tristegram_theme_sakura(tr::now),
		tr::lng_tristegram_theme_neon(tr::now),
		tr::lng_tristegram_theme_forest(tr::now),
		tr::lng_tristegram_theme_graphite(tr::now),
	};
}

void ApplyThemePreset(
		not_null<Window::SessionController*> controller,
		int index) {
	const auto &presets = ThemePresets();
	if (index < 0 || index >= int(presets.size())) {
		return;
	}
	const auto preset = presets[index];
	auto path = QString();
	for (const auto &scheme : Window::Theme::EmbeddedThemes()) {
		if (scheme.type == preset.type) {
			path = scheme.path;
			break;
		}
	}
	if (path.isEmpty()) {
		return;
	}
	auto &settings = Core::App().settings();
	settings.setSystemAccentColorEnabled(false);
	settings.themesAccentColors().set(preset.type, preset.accent);
	Local::writeSettings();

	const auto night = (preset.type != EmbeddedType::DayBlue)
		&& (preset.type != EmbeddedType::Default);
	if (Window::Theme::IsNightMode() == night) {
		Window::Theme::ApplyDefaultWithPath(path);
		Window::Theme::KeepApplied();
	} else {
		Window::Theme::ToggleNightModeWithConfirmation(
			&controller->window(),
			[=] {
				Window::Theme::ToggleNightMode(path);
				Window::Theme::KeepApplied();
			});
	}
}

std::vector<QString> BackgroundPresetNames() {
	return {
		tr::lng_tristegram_theme_embers(tr::now),
		tr::lng_tristegram_bg_sunset(tr::now),
		tr::lng_tristegram_theme_ocean(tr::now),
		tr::lng_tristegram_bg_aurora(tr::now),
		tr::lng_tristegram_theme_neon(tr::now),
		tr::lng_tristegram_theme_forest(tr::now),
		tr::lng_tristegram_bg_candy(tr::now),
		tr::lng_tristegram_theme_graphite(tr::now),
	};
}

void ApplyBackgroundPreset(
		not_null<Window::SessionController*> controller,
		int index) {
	const auto &presets = BackgroundPresets();
	if (index < 0 || index >= int(presets.size())) {
		return;
	}
	auto slug = QStringList();
	for (const auto &color : presets[index]) {
		slug.push_back(Hex(color));
	}
	const auto paper = Data::WallPaper::FromColorsSlug(slug.join('~'));
	if (!paper) {
		return;
	}
	controller->content()->setChatBackground(*paper);
}

void ChooseColor(
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		QColor current,
		Fn<void(QColor)> done) {
	controller->show(Box([=, title = std::move(title)](
			not_null<Ui::GenericBox*> box) mutable {
		const auto editor = box->addRow(object_ptr<ColorEditor>(
			box,
			ColorEditor::Mode::HSL,
			current));
		const auto save = [=] {
			const auto color = editor->color();
			box->closeBox();
			done(color);
		};
		editor->submitRequests(
		) | rpl::on_next(save, editor->lifetime());
		box->setFocusCallback([=] {
			editor->setInnerFocus();
		});
		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		box->setTitle(std::move(title));
		box->setWidth(editor->width());
	}));
}

} // namespace Tristegram
