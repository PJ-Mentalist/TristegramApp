/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_custom.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "core/application.h"
#include "data/data_thread.h"
#include "main/main_session.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_peer_menu.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "core/core_settings.h"
#include "extera/extera_config.h"
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
#include "styles/style_basic.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

#include <array>

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

// What a custom theme keeps besides the base theme and the accent.
enum class ValueType {
	Bool,
	Int,
	String,
};
struct ThemeValue {
	const char *key = nullptr;
	ValueType type = ValueType::Bool;
};
constexpr auto kThemeValues = std::array<ThemeValue, 26>{ {
	{ "tristegram_logo", ValueType::Int },
	{ "tristegram_logo_c1", ValueType::String },
	{ "tristegram_logo_c2", ValueType::String },
	{ "tristegram_logo_angle", ValueType::Int },
	{ "tristegram_logo_glow", ValueType::Bool },
	{ "tristegram_logo_glow_power", ValueType::Int },
	{ "tristegram_logo_pulse", ValueType::Bool },
	{ "tristegram_logo_shift", ValueType::Bool },
	{ "tristegram_bubble_gradient", ValueType::Bool },
	{ "tristegram_bubble_c1", ValueType::String },
	{ "tristegram_bubble_c2", ValueType::String },
	{ "tristegram_bubble_radius", ValueType::Int },
	{ "tristegram_icons_mode", ValueType::Int },
	{ "tristegram_icons_glow", ValueType::Int },
	{ "tristegram_icons_c1", ValueType::String },
	{ "tristegram_icons_c2", ValueType::String },
	{ "tristegram_tiles_mode", ValueType::Int },
	{ "tristegram_tiles_c1", ValueType::String },
	{ "tristegram_tiles_c2", ValueType::String },
	{ "tristegram_name_mode", ValueType::Int },
	{ "tristegram_name_c1", ValueType::String },
	{ "tristegram_name_c2", ValueType::String },
	{ "tristegram_live_kind", ValueType::Int },
	{ "tristegram_player_skin", ValueType::Int },
	{ "tristegram_click_effect", ValueType::Int },
	{ "tristegram_avatar_ring", ValueType::Int },
} };

constexpr auto kCustomThemesKey = "tristegram_custom_themes";
constexpr auto kThemeCodePrefix = "tristegram-theme:";

[[nodiscard]] QJsonArray LoadCustomThemes() {
	return QJsonDocument::fromJson(
		Extera::String(QString::fromLatin1(kCustomThemesKey)).toUtf8()
	).array();
}

void SaveCustomThemes(const QJsonArray &themes) {
	Extera::Config::Instance().setString(
		QString::fromLatin1(kCustomThemesKey),
		QString::fromUtf8(
			QJsonDocument(themes).toJson(QJsonDocument::Compact)));
}

[[nodiscard]] QJsonObject CustomThemeAt(int index) {
	const auto themes = LoadCustomThemes();
	return (index >= 0 && index < themes.size())
		? themes[index].toObject()
		: QJsonObject();
}

[[nodiscard]] EmbeddedType CurrentEmbeddedType() {
	return Window::Theme::IsNightMode()
		? EmbeddedType::Night
		: EmbeddedType::Default;
}

void ApplyEmbedded(
		not_null<Window::SessionController*> controller,
		EmbeddedType type,
		QColor accent) {
	auto path = QString();
	for (const auto &scheme : Window::Theme::EmbeddedThemes()) {
		if (scheme.type == type) {
			path = scheme.path;
			break;
		}
	}
	if (path.isEmpty()) {
		return;
	}
	auto &settings = Core::App().settings();
	settings.setSystemAccentColorEnabled(false);
	if (accent.isValid()) {
		settings.themesAccentColors().set(type, accent);
	}
	Local::writeSettings();

	const auto night = (type != EmbeddedType::DayBlue)
		&& (type != EmbeddedType::Default);
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

} // namespace

std::vector<QString> CustomThemeNames() {
	auto result = std::vector<QString>();
	for (const auto &value : LoadCustomThemes()) {
		result.push_back(value.toObject().value(u"name"_q).toString());
	}
	return result;
}

QColor CustomThemeAccent(int index) {
	return QColor(CustomThemeAt(index).value(u"accent"_q).toString());
}

bool CustomThemeNight(int index) {
	return CustomThemeAt(index).value(u"night"_q).toBool();
}

std::vector<QColor> CustomThemeColors(int index) {
	const auto object = CustomThemeAt(index);
	const auto values = object.value(u"values"_q).toObject();
	auto result = std::vector<QColor>();
	for (const auto key : { "tristegram_logo_c1", "tristegram_logo_c2" }) {
		const auto color = QColor(
			values.value(QString::fromLatin1(key)).toString());
		if (color.isValid()) {
			result.push_back(color);
		}
	}
	return result;
}

void SaveCustomTheme(const QString &name) {
	auto values = QJsonObject();
	for (const auto &entry : kThemeValues) {
		const auto key = QString::fromLatin1(entry.key);
		switch (entry.type) {
		case ValueType::Bool: values.insert(key, Extera::Bool(key)); break;
		case ValueType::Int: values.insert(key, Extera::Int(key)); break;
		case ValueType::String: values.insert(key, Extera::String(key)); break;
		}
	}
	const auto type = CurrentEmbeddedType();
	const auto accent = Core::App().settings().themesAccentColors().get(type);
	auto background = QJsonArray();
	for (const auto &color : Window::Theme::Background()->paper().backgroundColors()) {
		background.push_back(color.name());
	}
	auto object = QJsonObject();
	object.insert(u"name"_q, name);
	object.insert(u"night"_q, (type == EmbeddedType::Night));
	object.insert(
		u"accent"_q,
		(accent ? *accent : st::windowActiveTextFg->c).name());
	object.insert(u"values"_q, values);
	if (!background.isEmpty()) {
		object.insert(u"background"_q, background);
	}
	auto themes = LoadCustomThemes();
	for (auto i = 0; i != themes.size(); ++i) {
		if (themes[i].toObject().value(u"name"_q).toString() == name) {
			themes.removeAt(i);
			break;
		}
	}
	themes.push_back(object);
	SaveCustomThemes(themes);
}

void ApplyCustomTheme(
		not_null<Window::SessionController*> controller,
		int index) {
	const auto object = CustomThemeAt(index);
	if (object.isEmpty()) {
		return;
	}
	const auto values = object.value(u"values"_q).toObject();
	auto &config = Extera::Config::Instance();
	for (const auto &entry : kThemeValues) {
		const auto key = QString::fromLatin1(entry.key);
		if (!values.contains(key)) {
			continue;
		}
		const auto value = values.value(key);
		switch (entry.type) {
		case ValueType::Bool: config.setBool(key, value.toBool()); break;
		case ValueType::Int: config.setInt(key, value.toInt()); break;
		case ValueType::String: config.setString(key, value.toString()); break;
		}
	}
	config.setInt(u"tristegram_theme_preset"_q, -1);
	auto background = std::vector<QColor>();
	for (const auto &value : object.value(u"background"_q).toArray()) {
		const auto color = QColor(value.toString());
		if (color.isValid()) {
			background.push_back(color);
		}
	}
	if (!background.empty()) {
		ApplyBackgroundColors(controller, background);
	}
	ApplyEmbedded(
		controller,
		object.value(u"night"_q).toBool()
			? EmbeddedType::Night
			: EmbeddedType::Default,
		QColor(object.value(u"accent"_q).toString()));
}

void RemoveCustomTheme(int index) {
	auto themes = LoadCustomThemes();
	if (index >= 0 && index < themes.size()) {
		themes.removeAt(index);
		SaveCustomThemes(themes);
	}
}

QString CustomThemeCode(int index) {
	const auto object = CustomThemeAt(index);
	if (object.isEmpty()) {
		return QString();
	}
	const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
	return QString::fromLatin1(kThemeCodePrefix)
		+ QString::fromLatin1(json.toBase64(
			QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool IsCustomThemeCode(const QString &text) {
	return text.contains(QString::fromLatin1(kThemeCodePrefix));
}

bool ImportCustomTheme(
		not_null<Window::SessionController*> controller,
		const QString &text) {
	const auto prefix = QString::fromLatin1(kThemeCodePrefix);
	const auto start = text.indexOf(prefix);
	if (start < 0) {
		return false;
	}
	auto end = start + prefix.size();
	while (end < text.size()
		&& (text[end].isLetterOrNumber()
			|| text[end] == '-'
			|| text[end] == '_')) {
		++end;
	}
	const auto encoded = text.mid(start + prefix.size(), end - start - prefix.size());
	const auto json = QByteArray::fromBase64(
		encoded.toLatin1(),
		QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	const auto object = QJsonDocument::fromJson(json).object();
	if (object.isEmpty()
		|| object.value(u"name"_q).toString().isEmpty()
		|| !object.value(u"values"_q).isObject()) {
		return false;
	}
	auto themes = LoadCustomThemes();
	const auto name = object.value(u"name"_q).toString();
	for (auto i = 0; i != themes.size(); ++i) {
		if (themes[i].toObject().value(u"name"_q).toString() == name) {
			themes.removeAt(i);
			break;
		}
	}
	themes.push_back(object);
	SaveCustomThemes(themes);
	ApplyCustomTheme(controller, int(themes.size()) - 1);
	return true;
}

void ApplyAccentColor(
		not_null<Window::SessionController*> controller,
		QColor accent) {
	Extera::Config::Instance().setInt(u"tristegram_theme_preset"_q, -1);
	ApplyEmbedded(controller, CurrentEmbeddedType(), accent);
}

QColor CurrentAccentColor() {
	const auto accent = Core::App().settings().themesAccentColors().get(
		CurrentEmbeddedType());
	return accent ? *accent : st::windowActiveTextFg->c;
}

QColor ThemePresetAccent(int index) {
	const auto &presets = ThemePresets();
	return (index >= 0 && index < int(presets.size()))
		? presets[index].accent
		: QColor();
}

bool ThemePresetNight(int index) {
	const auto &presets = ThemePresets();
	return (index >= 0 && index < int(presets.size()))
		&& (presets[index].type != EmbeddedType::Default)
		&& (presets[index].type != EmbeddedType::DayBlue);
}

std::vector<QColor> BackgroundPresetColors(int index) {
	const auto &presets = BackgroundPresets();
	return (index >= 0 && index < int(presets.size()))
		? presets[index]
		: std::vector<QColor>();
}

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
	ApplyBackgroundColors(controller, presets[index]);
}

void ApplyBackgroundColors(
		not_null<Window::SessionController*> controller,
		const std::vector<QColor> &colors) {
	if (colors.empty()) {
		return;
	}
	auto slug = QStringList();
	for (const auto &color : colors) {
		slug.push_back(Hex(color));
		if (slug.size() == 4) {
			break;
		}
	}
	const auto paper = Data::WallPaper::FromColorsSlug(slug.join('~'));
	if (!paper) {
		return;
	}
	controller->content()->setChatBackground(*paper);
}

void SendToChosenChat(
		not_null<Window::SessionController*> controller,
		const QString &text,
		Fn<void(not_null<Data::Thread*>)> after) {
	Window::ShowChooseRecipientBox(
		controller,
		[=](not_null<Data::Thread*> thread) {
			auto message = Api::MessageToSend(Api::SendAction(thread));
			message.textWithTags = TextWithTags{ text };
			thread->session().api().sendMessage(std::move(message));
			if (after) {
				after(thread);
			}
			controller->showToast(tr::lng_tgs_share_sent(tr::now));
			return true;
		});
}

void AskText(
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &placeholder,
		const QString &initial,
		Fn<void(QString)> done) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(placeholder),
			initial));
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		const auto submit = [=] {
			const auto value = field->getLastText().trimmed();
			if (value.isEmpty()) {
				field->showError();
				return;
			}
			const auto callback = done;
			box->closeBox();
			callback(value);
		};
		field->submits(
		) | rpl::on_next([=](Qt::KeyboardModifiers) {
			submit();
		}, field->lifetime());
		box->addButton(tr::lng_settings_save(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
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
