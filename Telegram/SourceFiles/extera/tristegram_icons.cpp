/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_icons.h"

#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "ui/style/style_core.h"
#include "ui/style/style_core_icon_effects.h"
#include "styles/style_basic.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <algorithm>
#include <array>
#include <vector>

namespace Tristegram::Icons {
namespace {

struct Entry {
	const style::internal::ColorData *data = nullptr;
	int index = 0; // area * 2 + (over ? 1 : 0)
};

std::vector<Entry> Entries;
std::array<style::IconEffect, kAreaCount * 2> Effects;
std::array<bool, kAreaCount * 2> Active = {};
unsigned int KeySeed = 0;
style::MenuTileColors Tiles;

[[nodiscard]] QString AreaName(Area area) {
	switch (area) {
	case Area::Menu: return u"menu"_q;
	case Area::Compose: return u"compose"_q;
	case Area::Folders: return u"folders"_q;
	case Area::Buttons: return u"buttons"_q;
	}
	return u"menu"_q;
}

[[nodiscard]] QColor ColorValue(const QString &key, QColor fallback) {
	const auto color = QColor(Extera::String(key));
	return color.isValid() ? color : fallback;
}

void Add(const style::color &color, Area area, bool over) {
	Entries.push_back({ color.get(), int(area) * 2 + (over ? 1 : 0) });
}

void FillEntries() {
	Entries.clear();
	Add(st::windowBoldFg, Area::Menu, false);
	Add(st::windowFg, Area::Menu, false);
	Add(st::menuIconFg, Area::Menu, false);
	Add(st::menuIconFgOver, Area::Menu, true);
	Add(st::dialogsMenuIconFg, Area::Menu, false);
	Add(st::dialogsMenuIconFgOver, Area::Menu, true);
	Add(st::historyComposeIconFg, Area::Compose, false);
	Add(st::historyComposeIconFgOver, Area::Compose, true);
	Add(st::historyToDownFg, Area::Compose, false);
	Add(st::historyToDownFgOver, Area::Compose, true);
	Add(st::historySendIconFg, Area::Compose, false);
	Add(st::historySendIconFgOver, Area::Compose, true);
	Add(st::emojiIconFg, Area::Compose, false);
	Add(st::sideBarIconFg, Area::Folders, false);
	Add(st::boxTitleCloseFg, Area::Buttons, false);
	Add(st::boxTitleCloseFgOver, Area::Buttons, true);
}

[[nodiscard]] QColor Lighter(QColor color) {
	return color.isValid() ? color.lighter(115) : color;
}

void Rebuild() {
	const auto logo = Tristegram::CurrentLogoStyle();
	for (auto i = 0; i != kAreaCount; ++i) {
		const auto area = Area(i);
		const auto own = Extera::Int(AreaKey(area, u"mode"_q));
		const auto key = [&](const QString &suffix) {
			return own ? AreaKey(area, suffix) : GlobalKey(suffix);
		};
		const auto mode = own
			? (own - 1)
			: Extera::Int(GlobalKey(u"mode"_q));
		const auto glow = std::clamp(
			Extera::Int(key(u"glow"_q)),
			0,
			100) / 100.;
		const auto c1 = ColorValue(key(u"c1"_q), QColor(0x2F, 0x8C, 0xF0));
		const auto c2 = ColorValue(key(u"c2"_q), QColor(0x8B, 0x5C, 0xF6));
		auto effect = style::IconEffect();
		effect.glow = glow;
		auto active = true;
		switch (mode) {
		case 1:
			effect.from = st::windowActiveTextFg->c;
			break;
		case 2:
			effect.from = c1;
			break;
		case 3:
			effect.from = logo.from;
			effect.to = logo.to;
			effect.angle = logo.angle;
			break;
		case 4:
			effect.from = c1;
			effect.to = c2;
			break;
		default:
			active = (glow > 0.);
			break;
		}
		effect.key = ++KeySeed;
		Effects[i * 2] = effect;
		Active[i * 2] = active;

		auto over = effect;
		over.from = Lighter(effect.from);
		over.to = Lighter(effect.to);
		over.key = ++KeySeed;
		Effects[i * 2 + 1] = over;
		Active[i * 2 + 1] = active;
	}
	{
		// Tiles under menu icons: 0 keeps the colorful default.
		const auto c1 = ColorValue(u"tristegram_tiles_c1"_q, QColor(0xF0, 0x7A, 0x0C));
		const auto c2 = ColorValue(u"tristegram_tiles_c2"_q, QColor(0xE8, 0x31, 0x8A));
		auto tiles = style::MenuTileColors();
		switch (Extera::Int(u"tristegram_tiles_mode"_q)) {
		case 1:
			tiles.from = st::windowActiveTextFg->c.lighter(112);
			tiles.to = st::windowActiveTextFg->c;
			break;
		case 2:
			tiles.from = c1;
			tiles.to = c1;
			break;
		case 3:
			tiles.from = logo.from;
			tiles.to = logo.to;
			break;
		case 4:
			tiles.from = c1;
			tiles.to = c2;
			break;
		case 5:
			tiles.from = QColor(255, 255, 255, 30);
			tiles.to = QColor(255, 255, 255, 18);
			tiles.icon = st::windowFg->c;
			break;
		}
		Tiles = tiles;
	}
	style::RefreshIconEffects();
	for (const auto widget : QApplication::topLevelWidgets()) {
		widget->update();
	}
}

} // namespace

QString GlobalKey(const QString &suffix) {
	return u"tristegram_icons_"_q + suffix;
}

QString AreaKey(Area area, const QString &suffix) {
	return u"tristegram_icons_"_q + AreaName(area) + '_' + suffix;
}

void Init(rpl::lifetime &lifetime) {
	FillEntries();
	Rebuild();
	// The saved theme is read a bit later, take its colors then.
	crl::on_main([] { Rebuild(); });
	style::SetIconEffectProvider([](
			const style::internal::ColorData *data)
			-> const style::IconEffect* {
		for (const auto &entry : Entries) {
			if (entry.data == data) {
				return Active[entry.index] ? &Effects[entry.index] : nullptr;
			}
		}
		return nullptr;
	});
	style::SetMenuTileProvider([] { return Tiles; });
	Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"tristegram_icons_"_q)
			|| key.startsWith(u"tristegram_tiles_"_q)
			|| key.startsWith(u"tristegram_logo"_q);
	}) | rpl::on_next([] {
		Rebuild();
	}, lifetime);
	style::PaletteChanged(
	) | rpl::on_next([] {
		Rebuild();
	}, lifetime);
}

} // namespace Tristegram::Icons
