/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_settings_window.h"

#include "boxes/background_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "extera/extera_config.h"
#include "extera/tristegram_custom.h"
#include "extera/tristegram_cleaner.h"
#include "extera/tristegram_custom_ui.h"
#include "extera/tristegram_folders.h"
#include "extera/tristegram_fonts.h"
#include "extera/tristegram_live_wallpaper.h"
#include "extera/tristegram_logo.h"
#include "extera/tristegram_icons.h"
#include "ui/boxes/single_choice_box.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "settings/sections/settings_active_sessions.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_chat.h"
#include "settings/sections/settings_extera.h"
#include "settings/sections/settings_folders.h"
#include "settings/sections/settings_information.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_notifications.h"
#include "settings/sections/settings_privacy_security.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_filter_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>
#include <QtGui/QPolygonF>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Tristegram {
namespace {

using Phrase = tr::phrase<>;

bool BypassOnce = false;

enum class Section {
	Look,
	Logo,
	Fonts,
	Themes,
	Background,
	Animations,
	Avatars,
	Icons,
	Chats,
	Privacy,
	Eco,
	Accounts,
};

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

[[nodiscard]] Extera::Config &Config() {
	return Extera::Config::Instance();
}

[[nodiscard]] QColor ConfigColor(const QString &key, QColor fallback) {
	const auto result = QColor(Extera::String(key));
	return result.isValid() ? result : fallback;
}

void RepaintOnConfig(not_null<Ui::RpWidget*> widget) {
	Config().changes(
	) | rpl::on_next([=] {
		widget->update();
	}, widget->lifetime());
}

void PaintRowHover(QPainter &p, QRect rect, bool over) {
	if (!over) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(Alpha(st::windowFg->c, 0.05));
	const auto inner = QRectF(rect).marginsRemoved(QMarginsF(4, 2, 4, 2));
	p.drawRoundedRect(inner, Px(10), Px(10));
}

// Rounded card that holds rows, like grouped settings on iOS.
[[nodiscard]] not_null<Ui::VerticalLayout*> AddGroup(
		not_null<Ui::VerticalLayout*> content) {
	const auto group = content->add(
		object_ptr<Ui::VerticalLayout>(content),
		QMargins(Px(4), 0, Px(4), Px(6)));
	group->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(group);
		auto hq = PainterHighQualityEnabler(p);
		const auto rect = QRectF(group->rect()).marginsRemoved(
			QMarginsF(0.5, 0.5, 0.5, 0.5));
		p.setPen(QPen(Alpha(st::windowFg->c, 0.07), 1.));
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect, Px(14), Px(14));
	}, group->lifetime());
	return group;
}

void AddLabel(not_null<Ui::VerticalLayout*> content, const QString &text) {
	const auto upper = text.toUpper();
	const auto label = content->add(
		object_ptr<Ui::FixedHeightWidget>(content, Px(30)),
		QMargins(Px(8), 0, Px(8), 0));
	label->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(label);
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			label->rect().marginsRemoved(QMargins(0, 0, 0, Px(6))),
			Qt::AlignLeft | Qt::AlignBottom,
			upper);
	}, label->lifetime());
}

void AddNote(not_null<Ui::VerticalLayout*> content, const QString &text) {
	content->add(
		object_ptr<Ui::FlatLabel>(
			content,
			text,
			st::boxDividerLabel),
		QMargins(Px(8), Px(2), Px(8), Px(8)));
}

void AddSkip(not_null<Ui::VerticalLayout*> content, int height = 8) {
	content->add(object_ptr<Ui::FixedHeightWidget>(content, Px(height)));
}

[[nodiscard]] not_null<Ui::AbstractButton*> AddRowBase(
		not_null<Ui::VerticalLayout*> group,
		int height = 44) {
	const auto row = group->add(
		object_ptr<Ui::FixedHeightWidget>(group, Px(height)));
	const auto button = Ui::CreateChild<Ui::AbstractButton>(row);
	row->sizeValue(
	) | rpl::on_next([=](QSize size) {
		button->setGeometry(QRect(QPoint(), size));
	}, button->lifetime());
	return button;
}

void AddToggleRow(
		not_null<Ui::VerticalLayout*> group,
		const QString &title,
		Fn<bool()> get,
		Fn<void(bool)> set) {
	const auto button = AddRowBase(group);
	const auto &toggleSt = st::settingsButtonNoIcon.toggle;
	const auto view = button->lifetime().make_state<Ui::ToggleView>(
		toggleSt,
		get(),
		[=] { button->update(); });
	button->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(button);
		PaintRowHover(p, button->rect(), button->isOver());
		const auto size = view->getSize();
		const auto right = Px(14);
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		const auto text = QRect(
			Px(14),
			0,
			button->width() - Px(14) - right - size.width() - Px(10),
			button->height());
		p.drawText(
			text,
			Qt::AlignLeft | Qt::AlignVCenter,
			st::normalFont->elided(title, text.width()));
		view->paint(
			p,
			button->width() - right - size.width(),
			(button->height() - size.height()) / 2,
			button->width());
	}, button->lifetime());
	button->setClickedCallback([=] {
		set(!get());
		view->setChecked(get(), anim::type::normal);
	});
	Config().changes(
	) | rpl::on_next([=] {
		if (view->checked() != get()) {
			view->setChecked(get(), anim::type::normal);
		}
	}, button->lifetime());
}

void AddConfigToggle(
		not_null<Ui::VerticalLayout*> group,
		const QString &title,
		const QString &key) {
	AddToggleRow(
		group,
		title,
		[=] { return Extera::Bool(key); },
		[=](bool value) { Config().setBool(key, value); });
}

void AddActionRow(
		not_null<Ui::VerticalLayout*> group,
		const QString &title,
		Fn<QString()> value,
		Fn<void()> callback,
		bool accent = false) {
	const auto button = AddRowBase(group);
	button->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(button);
		PaintRowHover(p, button->rect(), button->isOver());
		p.setFont(st::normalFont);
		const auto right = value ? value() : QString();
		const auto chevron = QString(QChar(0x203A));
		const auto chevronWidth = st::semiboldFont->width(chevron);
		const auto rightWidth = right.isEmpty()
			? 0
			: (st::normalFont->width(right) + Px(8));
		p.setPen(accent ? st::windowActiveTextFg : st::windowFg);
		const auto text = QRect(
			Px(14),
			0,
			button->width() - Px(28) - chevronWidth - rightWidth,
			button->height());
		p.drawText(
			text,
			Qt::AlignLeft | Qt::AlignVCenter,
			st::normalFont->elided(title, text.width()));
		p.setPen(st::windowSubTextFg);
		if (!right.isEmpty()) {
			p.drawText(
				QRect(
					button->width() - Px(14) - chevronWidth - rightWidth,
					0,
					rightWidth - Px(8),
					button->height()),
				Qt::AlignRight | Qt::AlignVCenter,
				right);
		}
		p.setFont(st::semiboldFont);
		p.drawText(
			QRect(
				button->width() - Px(14) - chevronWidth,
				0,
				chevronWidth,
				button->height()),
			Qt::AlignRight | Qt::AlignVCenter,
			chevron);
	}, button->lifetime());
	button->setClickedCallback(callback);
	RepaintOnConfig(button);
}

void AddColorRow(
		not_null<Ui::VerticalLayout*> group,
		not_null<Window::SessionController*> controller,
		const QString &title,
		const QString &key,
		QColor fallback,
		Fn<void()> changed = nullptr) {
	const auto button = AddRowBase(group);
	button->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(button);
		PaintRowHover(p, button->rect(), button->isOver());
		const auto color = ConfigColor(key, fallback);
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		const auto swatch = Px(24);
		const auto text = QRect(
			Px(14),
			0,
			button->width() - Px(40) - swatch - Px(60),
			button->height());
		p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, title);
		p.setPen(st::windowSubTextFg);
		const auto hex = color.name().toUpper();
		p.drawText(
			QRect(
				button->width() - Px(14) - swatch - Px(80),
				0,
				Px(72),
				button->height()),
			Qt::AlignRight | Qt::AlignVCenter,
			hex);
		auto hq = PainterHighQualityEnabler(p);
		const auto circle = QRectF(
			button->width() - Px(14) - swatch,
			(button->height() - swatch) / 2.,
			swatch,
			swatch);
		p.setPen(QPen(Alpha(st::windowFg->c, 0.25), 1.5));
		p.setBrush(color);
		p.drawEllipse(circle);
	}, button->lifetime());
	button->setClickedCallback([=] {
		ChooseColor(
			controller,
			rpl::single(title),
			ConfigColor(key, fallback),
			[=](QColor color) {
				Config().setString(key, color.name());
				if (changed) {
					changed();
				}
			});
	});
	RepaintOnConfig(button);
}

// Title with the value on the right and a gradient track below it.
void AddSliderRow(
		not_null<Ui::VerticalLayout*> group,
		const QString &title,
		std::vector<int> values,
		Fn<QString(int)> text,
		Fn<int()> get,
		Fn<void(int)> set,
		bool live) {
	const auto row = group->add(
		object_ptr<Ui::FixedHeightWidget>(group, Px(62)));
	struct State {
		int index = 0;
		bool dragging = false;
	};
	const auto state = row->lifetime().make_state<State>();
	const auto count = int(values.size());
	const auto closest = [=](int value) {
		auto best = 0;
		for (auto i = 0; i != count; ++i) {
			if (std::abs(values[i] - value) < std::abs(values[best] - value)) {
				best = i;
			}
		}
		return best;
	};
	state->index = closest(get());
	const auto track = [=] {
		return QRect(
			Px(20),
			Px(40),
			row->width() - Px(40),
			Px(6));
	};
	const auto indexAt = [=](int x) {
		const auto rect = track();
		const auto ratio = std::clamp(
			(x - rect.x()) / double(std::max(rect.width(), 1)),
			0.,
			1.);
		return int(std::round(ratio * (count - 1)));
	};
	row->setMouseTracking(true);
	row->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(row);
		auto hq = PainterHighQualityEnabler(p);
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(Px(14), Px(6), row->width() - Px(28), Px(24)),
			Qt::AlignLeft | Qt::AlignVCenter,
			title);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(Px(14), Px(6), row->width() - Px(28), Px(24)),
			Qt::AlignRight | Qt::AlignVCenter,
			text(values[state->index]));
		const auto rect = QRectF(track());
		const auto radius = rect.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.12));
		p.drawRoundedRect(rect, radius, radius);
		const auto ratio = (count > 1)
			? (state->index / double(count - 1))
			: 0.;
		const auto style = CurrentLogoStyle();
		auto fill = QLinearGradient(rect.topLeft(), rect.topRight());
		fill.setColorAt(0., style.from);
		fill.setColorAt(1., style.to);
		auto filled = rect;
		filled.setWidth(std::max(rect.width() * ratio, rect.height()));
		p.setBrush(fill);
		p.drawRoundedRect(filled, radius, radius);
		const auto knob = Px(18);
		const auto center = QPointF(
			rect.x() + rect.width() * ratio,
			rect.center().y());
		p.setBrush(Alpha(style.from, 0.25));
		p.drawEllipse(center, knob * 0.75, knob * 0.75);
		p.setBrush(QColor(255, 255, 255));
		p.drawEllipse(center, knob / 2., knob / 2.);
	}, row->lifetime());
	const auto apply = [=](int index, bool final) {
		if (index != state->index) {
			state->index = index;
			row->update();
			if (live) {
				set(values[index]);
			}
		}
		if (final && !live) {
			set(values[state->index]);
		}
	};
	row->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			state->dragging = true;
			apply(indexAt(mouse->pos().x()), false);
		} else if (type == QEvent::MouseMove && state->dragging) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			apply(indexAt(mouse->pos().x()), false);
		} else if (type == QEvent::MouseButtonRelease && state->dragging) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			state->dragging = false;
			apply(indexAt(mouse->pos().x()), true);
		}
	}, row->lifetime());
}

void AddConfigSlider(
		not_null<Ui::VerticalLayout*> group,
		const QString &title,
		const QString &key,
		std::vector<int> values,
		Fn<QString(int)> text,
		bool live = true) {
	AddSliderRow(
		group,
		title,
		std::move(values),
		std::move(text),
		[=] { return Extera::Int(key); },
		[=](int value) { Config().setInt(key, value); },
		live);
}

[[nodiscard]] std::vector<int> Range(int from, int till, int step) {
	auto result = std::vector<int>();
	for (auto i = from; i <= till; i += step) {
		result.push_back(i);
	}
	return result;
}

void AddSegments(
		not_null<Ui::VerticalLayout*> content,
		std::vector<QString> labels,
		int active,
		Fn<void(int)> changed) {
	content->add(
		CreateSegmentedTabs(
			content,
			std::move(labels),
			active,
			std::move(changed)),
		QMargins(0, 0, 0, Px(4)));
}

// Gradient strip preview for two colors.
void AddGradientStrip(
		not_null<Ui::VerticalLayout*> group,
		Fn<std::vector<QColor>()> colors) {
	const auto strip = group->add(
		object_ptr<Ui::FixedHeightWidget>(group, Px(40)));
	strip->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(strip);
		auto hq = PainterHighQualityEnabler(p);
		const auto list = colors();
		if (list.empty()) {
			return;
		}
		const auto rect = QRectF(strip->rect()).marginsRemoved(
			QMarginsF(Px(14), Px(8), Px(14), Px(8)));
		auto fill = QLinearGradient(rect.topLeft(), rect.topRight());
		for (auto i = 0; i != int(list.size()); ++i) {
			fill.setColorAt(
				(list.size() > 1) ? (i / double(list.size() - 1)) : 0.,
				list[i]);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(fill);
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
	}, strip->lifetime());
	RepaintOnConfig(strip);
}

// Card on top of the fonts section: the title, a sample and a bubble
// in the current interface font.
void AddFontPreview(not_null<Ui::VerticalLayout*> content) {
	const auto card = content->add(
		object_ptr<Ui::FixedHeightWidget>(content, Px(168)),
		QMargins(Px(4), 0, Px(4), Px(6)));
	card->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(card);
		auto hq = PainterHighQualityEnabler(p);
		const auto style = CurrentLogoStyle();
		const auto rect = QRectF(card->rect()).marginsRemoved(
			QMarginsF(0.5, 0.5, 0.5, 0.5));
		auto bg = QLinearGradient(rect.topLeft(), rect.bottomRight());
		bg.setColorAt(0., Alpha(style.from, 0.10));
		bg.setColorAt(1., Alpha(style.to, 0.18));
		p.setPen(QPen(Alpha(st::windowFg->c, 0.08), 1.));
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect, Px(18), Px(18));
		p.setBrush(bg);
		p.drawRoundedRect(rect, Px(18), Px(18));

		const auto family = CurrentFontFamily();
		auto title = QFont(family.isEmpty() ? st::normalFont->f.family() : family);
		title.setPixelSize(Px(26));
		title.setWeight(QFont::Bold);
		p.setFont(title);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(Px(20), Px(14), card->width() - Px(40), Px(36)),
			Qt::AlignLeft | Qt::AlignVCenter,
			u"Tristegram"_q);
		auto body = QFont(title);
		body.setPixelSize(Px(14));
		body.setWeight(QFont::Normal);
		p.setFont(body);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(Px(20), Px(52), card->width() - Px(40), Px(40)),
			Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
			tr::lng_tgs_font_sample(tr::now));
		const auto bubbleText = tr::lng_tgs_font_bubble(tr::now);
		const auto metrics = QFontMetrics(body);
		const auto bubble = QRectF(
			Px(20),
			Px(100),
			std::min(
				metrics.horizontalAdvance(bubbleText) + Px(28),
				card->width() - Px(40)),
			Px(48));
		p.setPen(QPen(Alpha(st::windowFg->c, 0.10), 1.));
		p.setBrush(Alpha(st::windowBg->c, 0.85));
		p.drawRoundedRect(bubble, Px(16), Px(16));
		p.setPen(st::windowFg);
		p.drawText(
			bubble.toRect().marginsRemoved(QMargins(Px(14), Px(6), Px(14), Px(18))),
			Qt::AlignLeft | Qt::AlignVCenter,
			bubbleText);
		auto small = QFont(body);
		small.setPixelSize(Px(10));
		p.setFont(small);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			bubble.toRect().marginsRemoved(QMargins(Px(14), 0, Px(10), Px(4))),
			Qt::AlignRight | Qt::AlignBottom,
			u"12:36"_q);
	}, card->lifetime());
}

// Mini chat with the chosen background colors.
void AddBackgroundPreview(not_null<Ui::VerticalLayout*> content) {
	const auto card = content->add(
		object_ptr<Ui::FixedHeightWidget>(content, Px(150)),
		QMargins(Px(4), 0, Px(4), Px(6)));
	card->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(card);
		auto hq = PainterHighQualityEnabler(p);
		const auto rect = QRectF(card->rect());
		auto colors = std::vector<QColor>();
		const auto mode = Extera::Int(u"tristegram_bg_mode"_q);
		const auto preset = Extera::Int(u"tristegram_bg_preset"_q);
		if (mode == 0 && preset >= 0) {
			colors = BackgroundPresetColors(preset);
		}
		if (colors.empty()) {
			colors = {
				ConfigColor(u"tristegram_bg_c1"_q, QColor(0xF0, 0x7A, 0x0C)),
				ConfigColor(u"tristegram_bg_c2"_q, QColor(0xE8, 0x31, 0x8A)),
				ConfigColor(u"tristegram_bg_c3"_q, QColor(0x5B, 0x5C, 0xF6)),
			};
		}
		auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
		for (auto i = 0; i != int(colors.size()); ++i) {
			fill.setColorAt(
				(colors.size() > 1) ? (i / double(colors.size() - 1)) : 0.,
				colors[i]);
		}
		auto path = QPainterPath();
		path.addRoundedRect(rect, Px(18), Px(18));
		p.fillPath(path, fill);
		const auto paintBubble = [&](QString text, bool out, int top) {
			const auto width = st::normalFont->width(text) + Px(24);
			const auto bubble = QRectF(
				out ? (rect.width() - Px(16) - width) : Px(16),
				top,
				width,
				Px(34));
			p.setPen(Qt::NoPen);
			p.setBrush(out
				? QColor(255, 255, 255, 235)
				: QColor(20, 20, 24, 200));
			p.drawRoundedRect(bubble, Px(14), Px(14));
			p.setFont(st::normalFont);
			p.setPen(out ? QColor(20, 20, 24) : QColor(255, 255, 255));
			p.drawText(bubble, Qt::AlignCenter, text);
		};
		paintBubble(tr::lng_tgs_bg_hello(tr::now), false, Px(30));
		paintBubble(tr::lng_tgs_bg_new(tr::now), true, Px(84));
	}, card->lifetime());
	RepaintOnConfig(card);
}

// Avatar sample with the chosen corners and ring.
void AddAvatarPreview(not_null<Ui::VerticalLayout*> content) {
	const auto card = content->add(
		object_ptr<Ui::FixedHeightWidget>(content, Px(110)),
		QMargins(Px(4), 0, Px(4), Px(6)));
	card->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(card);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(QPen(Alpha(st::windowFg->c, 0.07), 1.));
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			QRectF(card->rect()).marginsRemoved(QMarginsF(0.5, 0.5, 0.5, 0.5)),
			Px(18),
			Px(18));
		const auto style = CurrentLogoStyle();
		const auto corners = std::clamp(Extera::Int(u"avatar_corners"_q), 0, 28);
		const auto ring = Extera::Int(u"tristegram_avatar_ring"_q);
		const auto size = Px(64);
		const auto radius = size / 2. * corners / 28.;
		for (auto i = 0; i != 3; ++i) {
			const auto x = Px(24) + i * (size + Px(28));
			const auto avatar = QRectF(x, (card->height() - size) / 2., size, size);
			auto fill = QLinearGradient(avatar.topLeft(), avatar.bottomRight());
			const auto hue = (i * 110 + 20) % 360;
			fill.setColorAt(0., QColor::fromHsv(hue, 150, 240));
			fill.setColorAt(1., QColor::fromHsv((hue + 40) % 360, 200, 200));
			p.setPen(Qt::NoPen);
			p.setBrush(fill);
			p.drawRoundedRect(avatar, radius, radius);
			if (ring > 0) {
				auto pen = QPen();
				pen.setWidthF((ring == 1) ? 1.5 : 2.5);
				if (ring == 3) {
					auto brush = QLinearGradient(
						avatar.topLeft(),
						avatar.bottomRight());
					brush.setColorAt(0., style.from);
					brush.setColorAt(1., style.to);
					pen.setBrush(brush);
				} else {
					pen.setColor((ring == 1)
						? Alpha(st::windowFg->c, 0.35)
						: st::windowBgActive->c);
				}
				p.setPen(pen);
				p.setBrush(Qt::NoBrush);
				const auto outer = avatar.marginsAdded(QMarginsF(3, 3, 3, 3));
				const auto outerRadius = radius ? (radius + 3) : 0.;
				p.drawRoundedRect(outer, outerRadius, outerRadius);
			}
			if (i == 0 && Extera::Bool(u"tristegram_online_accent"_q)) {
				p.setPen(QPen(st::windowBgOver->c, 3.));
				p.setBrush(st::windowBgActive);
				const auto dot = Px(14);
				p.drawEllipse(QRectF(
					avatar.right() - dot + 2,
					avatar.bottom() - dot + 2,
					dot,
					dot));
			}
		}
	}, card->lifetime());
	RepaintOnConfig(card);
}

void AddTiles(
		not_null<Ui::VerticalLayout*> content,
		TileGridDescriptor &&descriptor) {
	content->add(
		CreateTileGrid(content, std::move(descriptor)),
		QMargins(0, 0, 0, Px(4)));
}

void AddLogoTiles(not_null<Ui::VerticalLayout*> content) {
	AddTiles(content, {
		.count = LogoPresetsCount() + 1,
		.name = [](int index) {
			static const auto names = std::vector<const Phrase*>{
				&tr::lng_tristegram_logo_p0,
				&tr::lng_tristegram_logo_p1,
				&tr::lng_tristegram_logo_p2,
				&tr::lng_tristegram_logo_p3,
				&tr::lng_tristegram_logo_p4,
				&tr::lng_tristegram_logo_p5,
				&tr::lng_tristegram_logo_p6,
				&tr::lng_tristegram_logo_p7,
				&tr::lng_tristegram_logo_p8,
				&tr::lng_tristegram_logo_p9,
				&tr::lng_tristegram_logo_p10,
				&tr::lng_tristegram_logo_p11,
				&tr::lng_tristegram_logo_custom,
			};
			return (index >= 0 && index < int(names.size()))
				? (*names[index])(tr::now)
				: QString();
		},
		.paintSwatch = [](QPainter &p, QRect rect, int index) {
			auto style = LogoPreset(index);
			style.glow = false;
			PaintLogo(p, QRectF(rect), style);
		},
		.selected = [] { return Extera::Int(u"tristegram_logo"_q); },
		.chosen = [](int index) {
			Config().setInt(u"tristegram_logo"_q, index);
		},
		.configKey = u"tristegram_logo"_q,
	});
}

void SwitchLogoToCustom() {
	if (Extera::Int(u"tristegram_logo"_q) != LogoCustomIndex()) {
		const auto style = LogoPreset(Extera::Int(u"tristegram_logo"_q));
		Config().setString(u"tristegram_logo_c1"_q, style.from.name());
		Config().setString(u"tristegram_logo_c2"_q, style.to.name());
		Config().setInt(u"tristegram_logo"_q, LogoCustomIndex());
	}
}

void OpenNative(
		not_null<Window::SessionController*> controller,
		Settings::Type type,
		bool bypass = false) {
	controller->hideLayer(anim::type::instant);
	BypassOnce = bypass;
	controller->showSettings(type);
	BypassOnce = false;
}

void FillLook(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller,
		Fn<void(Section)> go) {
	content->add(CreateLivePreview(content), QMargins());
	AddSkip(content, 6);
	{
		const auto group = AddGroup(content);
		AddToggleRow(
			group,
			tr::lng_tgs_night(tr::now),
			[] { return Window::Theme::IsNightMode(); },
			[=](bool) {
				Window::Theme::ToggleNightModeWithConfirmation(
					&controller->window(),
					[] { Window::Theme::ToggleNightMode(); });
			});
		AddActionRow(
			group,
			tr::lng_tgs_logo_setup(tr::now),
			nullptr,
			[=] { go(Section::Logo); },
			true);
	}
	AddLabel(content, tr::lng_tgs_glass(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tristegram_glass_menus(tr::now), u"tristegram_glass_menus"_q);
		AddConfigToggle(group, tr::lng_tristegram_ios_icons(tr::now), u"tristegram_ios_icons"_q);
		AddConfigToggle(group, tr::lng_tristegram_compose_capsule(tr::now), u"tristegram_compose_capsule"_q);
	}
	AddLabel(content, tr::lng_tgs_new_design(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tgs_v_badges(tr::now), u"tristegram_gradient_badges"_q);
		AddConfigToggle(group, tr::lng_tgs_v_dates(tr::now), u"tristegram_date_capsules"_q);
		AddConfigToggle(group, tr::lng_tgs_v_active(tr::now), u"tristegram_gradient_active_chat"_q);
		AddConfigToggle(group, tr::lng_tgs_v_online(tr::now), u"tristegram_online_ring"_q);
		AddConfigToggle(group, tr::lng_tgs_v_send(tr::now), u"tristegram_send_capsule"_q);
		AddConfigToggle(group, tr::lng_tgs_v_files(tr::now), u"tristegram_file_tiles"_q);
		AddConfigToggle(group, tr::lng_tgs_v_appear(tr::now), u"tristegram_message_appear"_q);
		AddConfigToggle(group, tr::lng_tgs_v_bars(tr::now), u"tristegram_glass_bars"_q);
		AddConfigToggle(group, tr::lng_tgs_v_info(tr::now), u"tristegram_glass_info"_q);
		AddConfigToggle(group, tr::lng_tgs_v_glow(tr::now), u"tristegram_profile_glow"_q);
		AddConfigToggle(group, tr::lng_tgs_v_arrows(tr::now), u"tristegram_profile_arrows"_q);
		AddConfigToggle(group, tr::lng_tgs_v_music(tr::now), u"tristegram_header_music"_q);
		AddConfigToggle(group, tr::lng_tgs_v_hide_player(tr::now), u"tristegram_hide_tg_player"_q);
		AddConfigToggle(group, tr::lng_tgs_v_downloads(tr::now), u"tristegram_downloads_window"_q);
		AddConfigToggle(group, tr::lng_tgs_v_record(tr::now), u"tristegram_screen_record"_q);
		AddConfigToggle(group, tr::lng_tgs_v_scrollbar(tr::now), u"tristegram_gradient_scrollbar"_q);
		AddConfigToggle(group, tr::lng_tgs_v_typing(tr::now), u"tristegram_typing_gradient_v2"_q);
		AddConfigToggle(group, tr::lng_tgs_v_motion(tr::now), u"tristegram_chatlist_motion"_q);
		AddConfigToggle(group, tr::lng_tgs_v_shadow(tr::now), u"tristegram_soft_shadow"_q);
		AddConfigToggle(group, tr::lng_tgs_v_cards(tr::now), u"tristegram_reply_cards"_q);
		AddConfigToggle(group, tr::lng_tgs_v_voice(tr::now), u"tristegram_voice_gradient"_q);
		AddConfigToggle(group, tr::lng_tgs_v_reactions(tr::now), u"tristegram_glass_reactions"_q);
		AddConfigToggle(group, tr::lng_tgs_v_folders(tr::now), u"tristegram_folder_tiles"_q);
		AddConfigToggle(group, tr::lng_tgs_v_viewer(tr::now), u"tristegram_glass_viewer"_q);
		AddConfigToggle(group, tr::lng_tgs_v_tiles(tr::now), u"tristegram_menu_tiles"_q);
		AddConfigToggle(group, tr::lng_tgs_v_theme_fade(tr::now), u"tristegram_theme_fade"_q);
		AddConfigToggle(group, tr::lng_tgs_v_splash(tr::now), u"tristegram_splash"_q);
		AddConfigToggle(group, tr::lng_tgs_v_blur(tr::now), u"tristegram_blur_previews"_q);
		AddConfigToggle(group, tr::lng_tgs_v_templates(tr::now), u"tristegram_templates"_q);
		// TRISTEGRAM_NEW_TOGGLES
	}
	AddNote(content, tr::lng_tgs_new_design_note(tr::now));
	AddLabel(content, tr::lng_tgs_header(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tristegram_top_bar(tr::now), u"tristegram_top_bar"_q);
		AddConfigToggle(group, tr::lng_tristegram_bar_clock(tr::now), u"tristegram_bar_clock"_q);
		AddConfigToggle(group, tr::lng_tristegram_bar_seconds(tr::now), u"tristegram_bar_seconds"_q);
		AddConfigToggle(group, tr::lng_tristegram_bar_ping(tr::now), u"tristegram_bar_ping"_q);
		AddConfigToggle(group, tr::lng_tristegram_header_avatar(tr::now), u"tristegram_header_avatar"_q);
		AddConfigToggle(group, tr::lng_tristegram_header_center(tr::now), u"tristegram_header_center"_q);
	}
	AddNote(content, tr::lng_tgs_look_note(tr::now));
}

void FillLogo(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	content->add(CreateLivePreview(content), QMargins());
	AddNote(content, tr::lng_tristegram_preview_tap(tr::now));
	AddLabel(content, tr::lng_tgs_fill(tr::now));
	const auto solid = [] {
		const auto style = LogoPreset(Extera::Int(u"tristegram_logo"_q));
		return style.from == style.to;
	};
	AddSegments(
		content,
		{ tr::lng_tgs_fill_solid(tr::now), tr::lng_tgs_fill_gradient(tr::now) },
		solid() ? 0 : 1,
		[](int index) {
			SwitchLogoToCustom();
			const auto c1 = ConfigColor(
				u"tristegram_logo_c1"_q,
				QColor(0xF0, 0x7A, 0x0C));
			if (index == 0) {
				Config().setString(u"tristegram_logo_c2"_q, c1.name());
			} else if (ConfigColor(u"tristegram_logo_c2"_q, c1) == c1) {
				Config().setString(
					u"tristegram_logo_c2"_q,
					QColor(0xE8, 0x31, 0x8A).name());
			}
		});
	AddLabel(content, tr::lng_tgs_presets(tr::now));
	AddLogoTiles(content);
	AddLabel(content, tr::lng_tgs_own_colors(tr::now));
	{
		const auto group = AddGroup(content);
		AddGradientStrip(group, [] {
			const auto style = CurrentLogoStyle();
			return std::vector<QColor>{ style.from, style.to };
		});
		AddColorRow(
			group,
			controller,
			tr::lng_tristegram_logo_c1(tr::now),
			u"tristegram_logo_c1"_q,
			QColor(0xFF, 0x8A, 0x1F),
			[] { Config().setInt(u"tristegram_logo"_q, LogoCustomIndex()); });
		AddColorRow(
			group,
			controller,
			tr::lng_tristegram_logo_c2(tr::now),
			u"tristegram_logo_c2"_q,
			QColor(0xE8, 0x31, 0x8A),
			[] { Config().setInt(u"tristegram_logo"_q, LogoCustomIndex()); });
		AddConfigSlider(
			group,
			tr::lng_tristegram_logo_angle(tr::now),
			u"tristegram_logo_angle"_q,
			Range(0, 360, 15),
			[](int value) { return QString::number(value) + QChar(0x00B0); });
		AddConfigToggle(group, tr::lng_tristegram_logo_shift(tr::now), u"tristegram_logo_shift"_q);
	}
	AddLabel(content, tr::lng_tgs_glow(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tristegram_logo_glow(tr::now), u"tristegram_logo_glow"_q);
		AddConfigSlider(
			group,
			tr::lng_tristegram_logo_glow_power(tr::now),
			u"tristegram_logo_glow_power"_q,
			Range(10, 100, 5),
			[](int value) { return QString::number(value) + '%'; });
		AddConfigToggle(group, tr::lng_tristegram_logo_pulse(tr::now), u"tristegram_logo_pulse"_q);
	}
	AddLabel(content, tr::lng_tgs_animation(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tristegram_logo_anim(tr::now), u"tristegram_logo_anim"_q);
		AddConfigToggle(group, tr::lng_tristegram_logo_intro(tr::now), u"tristegram_logo_intro"_q);
		AddConfigToggle(group, tr::lng_tristegram_empty_logo(tr::now), u"tristegram_empty_logo"_q);
	}
	AddNote(content, tr::lng_tgs_logo_note(tr::now));
}

void FillFonts(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	AddFontPreview(content);
	AddLabel(content, tr::lng_tgs_font(tr::now));
	const auto families = std::make_shared<std::vector<QString>>(
		AvailableFontFamilies());
	const auto count = int(families->size()) + 2; // System + upload.
	AddTiles(content, {
		.count = count,
		.name = [=](int index) {
			if (index == 0) {
				return tr::lng_tgs_font_system(tr::now);
			} else if (index == count - 1) {
				return tr::lng_tgs_font_upload(tr::now);
			}
			return (*families)[index - 1];
		},
		.paintSwatch = [=](QPainter &p, QRect rect, int index) {
			if (index == count - 1) {
				p.setPen(QPen(st::windowActiveTextFg->c, 2.));
				const auto c = QRectF(rect).center();
				const auto s = rect.width() / 5.;
				p.drawLine(QPointF(c.x(), c.y() - s), QPointF(c.x(), c.y() + s));
				p.drawLine(QPointF(c.x() - s, c.y()), QPointF(c.x() + s, c.y()));
				return;
			}
			auto font = (index == 0)
				? QFont(st::normalFont->f)
				: QFont((*families)[index - 1]);
			font.setPixelSize(rect.height() * 2 / 3);
			font.setWeight(QFont::DemiBold);
			p.setFont(font);
			p.setPen(st::windowFg);
			p.drawText(rect, Qt::AlignCenter, u"Aa"_q);
		},
		.selected = [=] {
			const auto current = CurrentFontFamily();
			if (current.isEmpty()) {
				return 0;
			}
			const auto i = std::find(
				families->begin(),
				families->end(),
				current);
			return (i == families->end())
				? -1
				: int(i - families->begin()) + 1;
		},
		.chosen = [=](int index) {
			if (index == count - 1) {
				ChooseUserFont(controller);
			} else {
				ApplyFontFamily(
					controller,
					index ? (*families)[index - 1] : QString());
			}
		},
		.configKey = QString(),
	});
	AddNote(content, tr::lng_tgs_font_note(tr::now));

	// Tristegram: size and weight of every font, like on iPhone.
	AddLabel(content, tr::lng_tgs_font_size_title(tr::now));
	const auto group = AddGroup(content);
	AddConfigSlider(
		group,
		tr::lng_tgs_font_size(tr::now),
		u"tristegram_font_size"_q,
		Range(-2, 8, 1),
		[](int value) {
			return (value > 0)
				? (u"+"_q + QString::number(value) + u" px"_q)
				: (value < 0)
				? (QString::number(value) + u" px"_q)
				: tr::lng_tgs_font_size_default(tr::now);
		},
		false);
	AddLabel(content, tr::lng_tgs_font_weight(tr::now));
	AddSegments(
		content,
		{
			tr::lng_tgs_font_weight_normal(tr::now),
			tr::lng_tgs_font_weight_ios(tr::now),
			tr::lng_tgs_font_weight_bold(tr::now),
		},
		std::clamp(Extera::Int(u"tristegram_font_weight"_q), 0, 2),
		[](int index) {
			Config().setInt(u"tristegram_font_weight"_q, index);
		});
	const auto restart = AddGroup(content);
	AddActionRow(
		restart,
		tr::lng_tgs_restart_now(tr::now),
		nullptr,
		[] { Core::Restart(); },
		true);
	AddNote(content, tr::lng_tgs_font_size_note(tr::now));
}

void FillThemes(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	AddLabel(content, tr::lng_tgs_ready_themes(tr::now));
	AddTiles(content, {
		.count = int(ThemePresetNames().size()),
		.name = [](int index) {
			const auto names = ThemePresetNames();
			return (index >= 0 && index < int(names.size()))
				? names[index]
				: QString();
		},
		.paintSwatch = [](QPainter &p, QRect rect, int index) {
			const auto accent = ThemePresetAccent(index);
			const auto night = ThemePresetNight(index);
			auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
			fill.setColorAt(0., night ? QColor(0x1C, 0x1C, 0x22) : QColor(0xF4, 0xF4, 0xF6));
			fill.setColorAt(1., night ? QColor(0x2C, 0x2C, 0x34) : QColor(0xE2, 0xE2, 0xE8));
			p.setPen(Qt::NoPen);
			p.setBrush(fill);
			p.drawEllipse(rect);
			const auto dot = QRectF(rect).marginsRemoved(QMarginsF(
				rect.width() * 0.28,
				rect.height() * 0.28,
				rect.width() * 0.28,
				rect.height() * 0.28));
			p.setBrush(accent);
			p.drawEllipse(dot);
		},
		.selected = [] { return Extera::Int(u"tristegram_theme_preset"_q); },
		.chosen = [=](int index) {
			Config().setInt(u"tristegram_theme_preset"_q, index);
			ApplyThemePreset(controller, index);
		},
		.configKey = u"tristegram_theme_preset"_q,
	});
	AddLabel(content, tr::lng_tgs_bubbles(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tristegram_bubble_gradient(tr::now), u"tristegram_bubble_gradient"_q);
		AddGradientStrip(group, [] {
			return std::vector<QColor>{
				ConfigColor(u"tristegram_bubble_c1"_q, QColor(0xF0, 0x7A, 0x0C)),
				ConfigColor(u"tristegram_bubble_c2"_q, QColor(0xFF, 0x37, 0x5F)),
			};
		});
		AddColorRow(
			group,
			controller,
			tr::lng_tristegram_bubble_c1(tr::now),
			u"tristegram_bubble_c1"_q,
			QColor(0xF0, 0x7A, 0x0C),
			[] { Config().setBool(u"tristegram_bubble_gradient"_q, true); });
		AddColorRow(
			group,
			controller,
			tr::lng_tristegram_bubble_c2(tr::now),
			u"tristegram_bubble_c2"_q,
			QColor(0xFF, 0x37, 0x5F),
			[] { Config().setBool(u"tristegram_bubble_gradient"_q, true); });
	}
	AddNote(content, tr::lng_tristegram_bubbles_about(tr::now));
}

// Vector icons for the animated wallpaper tiles: emoji glyphs look
// different on every system and are missing on some.
void PaintLiveKindIcon(QPainter &p, QRectF rect, int kind, QColor color) {
	auto hq = PainterHighQualityEnabler(p);
	const auto c = rect.center();
	const auto s = rect.width() * 0.26; // half of the icon size
	const auto stroke = std::max(rect.width() / 18., 1.5);
	auto pen = QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	p.save();
	switch (LiveWallpaperKind(kind)) {
	case LiveWallpaperKind::None: {
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(c, s * 0.9, s * 0.9);
		p.drawLine(
			QPointF(c.x() - s * 0.64, c.y() + s * 0.64),
			QPointF(c.x() + s * 0.64, c.y() - s * 0.64));
	} break;
	case LiveWallpaperKind::Doves: {
		// A bird with raised wings and a small tail.
		auto path = QPainterPath();
		path.moveTo(c.x() - s, c.y() - s * 0.35);
		path.cubicTo(
			c.x() - s * 0.6, c.y() - s * 0.75,
			c.x() - s * 0.2, c.y() - s * 0.45,
			c.x(), c.y() + s * 0.05);
		path.cubicTo(
			c.x() + s * 0.2, c.y() - s * 0.45,
			c.x() + s * 0.6, c.y() - s * 0.75,
			c.x() + s, c.y() - s * 0.35);
		path.cubicTo(
			c.x() + s * 0.55, c.y() - s * 0.2,
			c.x() + s * 0.3, c.y() + s * 0.2,
			c.x() + s * 0.12, c.y() + s * 0.45);
		path.lineTo(c.x() + s * 0.32, c.y() + s * 0.85);
		path.lineTo(c.x(), c.y() + s * 0.62);
		path.lineTo(c.x() - s * 0.32, c.y() + s * 0.85);
		path.lineTo(c.x() - s * 0.12, c.y() + s * 0.45);
		path.cubicTo(
			c.x() - s * 0.3, c.y() + s * 0.2,
			c.x() - s * 0.55, c.y() - s * 0.2,
			c.x() - s, c.y() - s * 0.35);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawPath(path);
	} break;
	case LiveWallpaperKind::Stars: {
		const auto sparkle = [&](QPointF at, double r) {
			auto path = QPainterPath();
			path.moveTo(at.x(), at.y() - r);
			path.quadTo(at, QPointF(at.x() + r, at.y()));
			path.quadTo(at, QPointF(at.x(), at.y() + r));
			path.quadTo(at, QPointF(at.x() - r, at.y()));
			path.quadTo(at, QPointF(at.x(), at.y() - r));
			p.drawPath(path);
		};
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		sparkle(QPointF(c.x() - s * 0.2, c.y() + s * 0.15), s * 0.85);
		sparkle(QPointF(c.x() + s * 0.65, c.y() - s * 0.6), s * 0.38);
		sparkle(QPointF(c.x() + s * 0.7, c.y() + s * 0.7), s * 0.22);
	} break;
	case LiveWallpaperKind::Bubbles: {
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto bubble = [&](QPointF at, double r) {
			p.setPen(pen);
			p.drawEllipse(at, r, r);
			p.setPen(QPen(color, stroke * 0.8, Qt::SolidLine, Qt::RoundCap));
			p.drawArc(
				QRectF(at.x() - r * 0.6, at.y() - r * 0.6, r * 1.2, r * 1.2),
				100 * 16,
				60 * 16);
		};
		bubble(QPointF(c.x() - s * 0.3, c.y() + s * 0.25), s * 0.62);
		bubble(QPointF(c.x() + s * 0.55, c.y() - s * 0.45), s * 0.36);
		bubble(QPointF(c.x() + s * 0.6, c.y() + s * 0.65), s * 0.2);
	} break;
	case LiveWallpaperKind::Code: {
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto h = s * 0.55;
		p.drawPolyline(QPolygonF{
			QPointF(c.x() - s * 0.45, c.y() - h),
			QPointF(c.x() - s, c.y()),
			QPointF(c.x() - s * 0.45, c.y() + h),
		});
		p.drawPolyline(QPolygonF{
			QPointF(c.x() + s * 0.45, c.y() - h),
			QPointF(c.x() + s, c.y()),
			QPointF(c.x() + s * 0.45, c.y() + h),
		});
		p.drawLine(
			QPointF(c.x() + s * 0.2, c.y() - s * 0.75),
			QPointF(c.x() - s * 0.2, c.y() + s * 0.75));
	} break;
	case LiveWallpaperKind::Aurora: {
		p.setBrush(Qt::NoBrush);
		for (auto i = 0; i != 3; ++i) {
			const auto y = c.y() - s * 0.55 + i * s * 0.55;
			auto path = QPainterPath();
			path.moveTo(c.x() - s, y);
			path.cubicTo(
				c.x() - s * 0.45, y - s * 0.45,
				c.x() + s * 0.05, y + s * 0.4,
				c.x() + s, y - s * 0.15);
			auto faded = color;
			faded.setAlphaF(color.alphaF() * (1. - i * 0.25));
			p.setPen(QPen(faded, stroke, Qt::SolidLine, Qt::RoundCap));
			p.drawPath(path);
		}
	} break;
	case LiveWallpaperKind::Video: {
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		const auto frame = QRectF(
			c.x() - s,
			c.y() - s * 0.72,
			s * 2,
			s * 1.44);
		p.drawRoundedRect(frame, s * 0.3, s * 0.3);
		auto play = QPainterPath();
		play.moveTo(c.x() - s * 0.25, c.y() - s * 0.38);
		play.lineTo(c.x() + s * 0.42, c.y());
		play.lineTo(c.x() - s * 0.25, c.y() + s * 0.38);
		play.closeSubpath();
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawPath(play);
	} break;
	}
	p.restore();
}

void FillBackground(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	AddBackgroundPreview(content);
	AddLabel(content, tr::lng_tgs_bg_type(tr::now));
	const auto mode = content->lifetime().make_state<rpl::variable<int>>(
		std::clamp(Extera::Int(u"tristegram_bg_mode"_q), 0, 3));
	AddSegments(
		content,
		{
			tr::lng_tgs_bg_ready(tr::now),
			tr::lng_tgs_bg_gradient(tr::now),
			tr::lng_tgs_bg_photo(tr::now),
			tr::lng_tgl_tab(tr::now),
		},
		mode->current(),
		[=](int index) {
			*mode = index;
			Config().setInt(u"tristegram_bg_mode"_q, index);
		});
	const auto addWrap = [&](int index) {
		const auto wrap = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)));
		wrap->toggleOn(mode->value() | rpl::map(rpl::mappers::_1 == index));
		wrap->finishAnimating();
		return wrap->entity();
	};
	{
		const auto inner = addWrap(0);
		AddTiles(inner, {
			.count = int(BackgroundPresetNames().size()),
			.name = [](int index) {
				const auto names = BackgroundPresetNames();
				return (index >= 0 && index < int(names.size()))
					? names[index]
					: QString();
			},
			.paintSwatch = [](QPainter &p, QRect rect, int index) {
				const auto colors = BackgroundPresetColors(index);
				if (colors.empty()) {
					return;
				}
				auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
				for (auto i = 0; i != int(colors.size()); ++i) {
					fill.setColorAt(
						(colors.size() > 1) ? (i / double(colors.size() - 1)) : 0.,
						colors[i]);
				}
				p.setPen(Qt::NoPen);
				p.setBrush(fill);
				p.drawEllipse(rect);
			},
			.selected = [] { return Extera::Int(u"tristegram_bg_preset"_q); },
			.chosen = [=](int index) {
				Config().setInt(u"tristegram_bg_preset"_q, index);
				ApplyBackgroundPreset(controller, index);
			},
			.configKey = u"tristegram_bg_preset"_q,
		});
	}
	{
		const auto inner = addWrap(1);
		const auto group = AddGroup(inner);
		const auto colors = [] {
			return std::vector<QColor>{
				ConfigColor(u"tristegram_bg_c1"_q, QColor(0xF0, 0x7A, 0x0C)),
				ConfigColor(u"tristegram_bg_c2"_q, QColor(0xE8, 0x31, 0x8A)),
				ConfigColor(u"tristegram_bg_c3"_q, QColor(0x5B, 0x5C, 0xF6)),
			};
		};
		const auto apply = [=] {
			Config().setInt(u"tristegram_bg_preset"_q, -1);
			ApplyBackgroundColors(controller, colors());
		};
		AddGradientStrip(group, colors);
		AddColorRow(group, controller, tr::lng_tgs_color1(tr::now), u"tristegram_bg_c1"_q, QColor(0xF0, 0x7A, 0x0C), apply);
		AddColorRow(group, controller, tr::lng_tgs_color2(tr::now), u"tristegram_bg_c2"_q, QColor(0xE8, 0x31, 0x8A), apply);
		AddColorRow(group, controller, tr::lng_tgs_color3(tr::now), u"tristegram_bg_c3"_q, QColor(0x5B, 0x5C, 0xF6), apply);
		AddActionRow(group, tr::lng_tgs_apply(tr::now), nullptr, apply, true);
		AddNote(inner, tr::lng_tgs_bg_gradient_note(tr::now));
	}
	{
		const auto inner = addWrap(2);
		const auto group = AddGroup(inner);
		AddActionRow(
			group,
			tr::lng_tristegram_chat_bg(tr::now),
			nullptr,
			[=] { controller->show(Box<BackgroundBox>(controller)); },
			true);
		AddNote(inner, tr::lng_tgs_bg_photo_note(tr::now));
	}
	{
		// Tristegram: animated wallpapers.
		const auto inner = addWrap(3);
		const auto names = std::vector<QString>{
			tr::lng_tgl_none(tr::now),
			tr::lng_tgl_doves(tr::now),
			tr::lng_tgl_stars(tr::now),
			tr::lng_tgl_bubbles(tr::now),
			tr::lng_tgl_code(tr::now),
			tr::lng_tgl_aurora(tr::now),
			tr::lng_tgl_own(tr::now),
		};
		AddTiles(inner, {
			.count = int(names.size()),
			.name = [=](int index) { return names[index]; },
			.paintSwatch = [=](QPainter &p, QRect rect, int index) {
				const auto colors = CurrentLogoStyle();
				auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
				fill.setColorAt(0., index ? colors.from : Alpha(st::windowFg->c, 0.12));
				fill.setColorAt(1., index ? colors.to : Alpha(st::windowFg->c, 0.06));
				p.setPen(Qt::NoPen);
				p.setBrush(fill);
				p.drawEllipse(rect);
				PaintLiveKindIcon(
					p,
					QRectF(rect),
					index,
					index ? QColor(255, 255, 255) : st::windowSubTextFg->c);
			},
			.selected = [] {
				return std::clamp(Extera::Int(u"tristegram_live_kind"_q), 0, 6);
			},
			.chosen = [=](int index) {
				if (index == int(LiveWallpaperKind::Video)) {
					ChooseLiveWallpaperFile(controller);
				} else {
					Config().setInt(u"tristegram_live_kind"_q, index);
					controller->widget()->update();
				}
			},
			.configKey = u"tristegram_live_kind"_q,
		});
		AddLabel(inner, tr::lng_tgl_fps(tr::now));
		const auto values = std::vector<int>{ 15, 24, 30, 60 };
		const auto current = Extera::Int(u"tristegram_live_fps"_q);
		const auto i = std::find(values.begin(), values.end(), current);
		AddSegments(
			inner,
			{ u"15"_q, u"24"_q, u"30"_q, u"60"_q },
			(i == values.end()) ? 1 : int(i - values.begin()),
			[=](int index) {
				Config().setInt(u"tristegram_live_fps"_q, values[index]);
			});
		AddNote(inner, tr::lng_tgl_note(tr::now));
	}
}

void FillAnimations(not_null<Ui::VerticalLayout*> content) {
	content->add(CreateLivePreview(content), QMargins());
	AddLabel(content, tr::lng_tgs_animations(tr::now));
	{
		const auto group = AddGroup(content);
		AddToggleRow(
			group,
			tr::lng_tgs_animations_on(tr::now),
			[] { return !Extera::Bool(u"tristegram_eco"_q); },
			[](bool value) { Config().setBool(u"tristegram_eco"_q, !value); });
		AddConfigToggle(group, tr::lng_tristegram_logo_anim(tr::now), u"tristegram_logo_anim"_q);
		AddConfigToggle(group, tr::lng_tristegram_logo_intro(tr::now), u"tristegram_logo_intro"_q);
		AddConfigToggle(group, tr::lng_tristegram_logo_pulse(tr::now), u"tristegram_logo_pulse"_q);
		AddConfigToggle(group, tr::lng_tristegram_logo_shift(tr::now), u"tristegram_logo_shift"_q);
	}
	AddNote(content, tr::lng_tgs_animations_note(tr::now));

	// Tristegram: effects where you click and where you type.
	AddLabel(content, tr::lng_tgs_click_effect(tr::now));
	AddSegments(
		content,
		{
			tr::lng_tgs_effect_none(tr::now),
			tr::lng_tgs_effect_ring(tr::now),
			tr::lng_tgs_effect_sparks(tr::now),
			tr::lng_tgs_effect_hearts(tr::now),
			tr::lng_tgs_effect_confetti(tr::now),
		},
		std::clamp(Extera::Int(u"tristegram_click_effect"_q), 0, 4),
		[](int index) {
			Config().setInt(u"tristegram_click_effect"_q, index);
		});
	AddLabel(content, tr::lng_tgs_type_effect(tr::now));
	AddSegments(
		content,
		{
			tr::lng_tgs_effect_none(tr::now),
			tr::lng_tgs_effect_sparks(tr::now),
			tr::lng_tgs_effect_hearts(tr::now),
			tr::lng_tgs_effect_confetti(tr::now),
		},
		std::clamp(Extera::Int(u"tristegram_type_effect"_q), 0, 3),
		[](int index) {
			Config().setInt(u"tristegram_type_effect"_q, index);
		});
	AddNote(content, tr::lng_tgs_effects_note(tr::now));
}

void FillAvatars(not_null<Ui::VerticalLayout*> content) {
	AddAvatarPreview(content);
	AddLabel(content, tr::lng_tgs_shape(tr::now));
	const auto corners = Extera::Int(u"avatar_corners"_q);
	AddSegments(
		content,
		{
			tr::lng_tgs_shape_circle(tr::now),
			tr::lng_tgs_shape_rounded(tr::now),
			tr::lng_tgs_shape_square(tr::now),
		},
		(corners >= 24) ? 0 : (corners >= 8) ? 1 : 2,
		[](int index) {
			Config().setInt(
				u"avatar_corners"_q,
				(index == 0) ? 28 : (index == 1) ? 12 : 4);
		});
	AddLabel(content, tr::lng_tgs_frame(tr::now));
	AddSegments(
		content,
		{
			tr::lng_tristegram_ring_none(tr::now),
			tr::lng_tristegram_ring_thin(tr::now),
			tr::lng_tristegram_ring_accent(tr::now),
			tr::lng_tristegram_ring_gradient(tr::now),
		},
		std::clamp(Extera::Int(u"tristegram_avatar_ring"_q), 0, 3),
		[](int index) {
			Config().setInt(u"tristegram_avatar_ring"_q, index);
		});
	AddLabel(content, tr::lng_tgs_where(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tgs_sender_avatars(tr::now), u"sender_mini_avatars"_q);
		AddConfigToggle(group, tr::lng_tristegram_online_accent(tr::now), u"tristegram_online_accent"_q);
		AddConfigToggle(group, tr::lng_tristegram_private_avatars(tr::now), u"tristegram_private_avatars"_q);
		AddConfigToggle(group, tr::lng_tristegram_header_avatar(tr::now), u"tristegram_header_avatar"_q);
	}
}

// Row of real icons from every area, painted with the current look.
void AddIconsPreview(not_null<Ui::VerticalLayout*> content) {
	const auto group = AddGroup(content);
	const auto row = group->add(
		object_ptr<Ui::FixedHeightWidget>(group, Px(64)));
	row->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(row);
		const auto icons = std::vector<const style::icon*>{
			&st::menuIconReply,
			&st::menuIconCopy,
			&st::menuIconForward,
			&st::menuIconPin,
			&st::menuIconInfo,
			&st::historyAttach.icon,
			&st::foldersCat,
			&st::foldersBook,
			&st::boxTitleClose.icon,
		};
		const auto count = int(icons.size());
		const auto step = row->width() / double(count);
		for (auto i = 0; i != count; ++i) {
			const auto icon = icons[i];
			const auto x = int(step * i + (step - icon->width()) / 2.);
			const auto y = (row->height() - icon->height()) / 2;
			icon->paint(p, x, y, row->width());
		}
	}, row->lifetime());
	RepaintOnConfig(row);
}

[[nodiscard]] std::vector<QString> IconModeLabels() {
	return {
		tr::lng_tgs_icons_theme(tr::now),
		tr::lng_tgs_icons_accent(tr::now),
		tr::lng_tgs_icons_color(tr::now),
		tr::lng_tgs_icons_logo(tr::now),
		tr::lng_tgs_icons_gradient(tr::now),
	};
}

void AddIconColors(
		not_null<Ui::VerticalLayout*> group,
		not_null<Window::SessionController*> controller,
		Fn<QString(QString)> key) {
	AddColorRow(
		group,
		controller,
		tr::lng_tgs_icons_c1(tr::now),
		key(u"c1"_q),
		QColor(0x2F, 0x8C, 0xF0));
	AddColorRow(
		group,
		controller,
		tr::lng_tgs_icons_c2(tr::now),
		key(u"c2"_q),
		QColor(0x8B, 0x5C, 0xF6));
	AddConfigSlider(
		group,
		tr::lng_tgs_icons_glow(tr::now),
		key(u"glow"_q),
		Range(0, 100, 5),
		[](int value) { return QString::number(value) + '%'; });
}

void FillIcons(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	using namespace Tristegram::Icons;
	AddIconsPreview(content);
	AddLabel(content, tr::lng_tgs_icons_style(tr::now));
	AddSegments(
		content,
		IconModeLabels(),
		std::clamp(Extera::Int(GlobalKey(u"mode"_q)), 0, kModeCount - 1),
		[](int index) { Config().setInt(GlobalKey(u"mode"_q), index); });
	{
		const auto group = AddGroup(content);
		AddIconColors(group, controller, [](QString suffix) {
			return GlobalKey(suffix);
		});
	}
	AddNote(content, tr::lng_tgs_icons_note(tr::now));

	const auto areas = std::vector<std::pair<Area, QString>>{
		{ Area::Menu, tr::lng_tgs_icons_menu(tr::now) },
		{ Area::Compose, tr::lng_tgs_icons_compose(tr::now) },
		{ Area::Folders, tr::lng_tgs_icons_folders(tr::now) },
		{ Area::Buttons, tr::lng_tgs_icons_buttons(tr::now) },
	};
	for (const auto &entry : areas) {
		const auto area = entry.first;
		const auto title = entry.second;
		AddLabel(content, title);
		const auto group = AddGroup(content);
		const auto modeKey = AreaKey(area, u"mode"_q);
		auto texts = IconModeLabels();
		texts.insert(begin(texts), tr::lng_tgs_icons_common(tr::now));
		AddActionRow(
			group,
			tr::lng_tgs_icons_area_style(tr::now),
			[=] {
				const auto index = std::clamp(
					Extera::Int(modeKey),
					0,
					int(texts.size()) - 1);
				return texts[index];
			},
			[=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					SingleChoiceBox(box, {
						.title = rpl::single(title),
						.options = texts,
						.initialSelection = std::clamp(
							Extera::Int(modeKey),
							0,
							int(texts.size()) - 1),
						.callback = [=](int index) {
							Config().setInt(modeKey, index);
						},
					});
				}));
			});
		AddIconColors(group, controller, [=](QString suffix) {
			return AreaKey(area, suffix);
		});
	}
	AddNote(content, tr::lng_tgs_icons_areas_note(tr::now));

	AddLabel(content, tr::lng_tgs_tiles_title(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(
			group,
			tr::lng_tgs_v_tiles(tr::now),
			u"tristegram_menu_tiles"_q);
		const auto texts = std::vector<QString>{
			tr::lng_tgs_tiles_mixed(tr::now),
			tr::lng_tgs_icons_accent(tr::now),
			tr::lng_tgs_icons_color(tr::now),
			tr::lng_tgs_icons_logo(tr::now),
			tr::lng_tgs_icons_gradient(tr::now),
			tr::lng_tgs_tiles_glass(tr::now),
		};
		const auto modeKey = u"tristegram_tiles_mode"_q;
		const auto title = tr::lng_tgs_tiles_title(tr::now);
		AddActionRow(
			group,
			tr::lng_tgs_tiles_style(tr::now),
			[=] {
				return texts[std::clamp(
					Extera::Int(modeKey),
					0,
					int(texts.size()) - 1)];
			},
			[=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					SingleChoiceBox(box, {
						.title = rpl::single(title),
						.options = texts,
						.initialSelection = std::clamp(
							Extera::Int(modeKey),
							0,
							int(texts.size()) - 1),
						.callback = [=](int index) {
							Config().setInt(modeKey, index);
						},
					});
				}));
			});
		AddColorRow(
			group,
			controller,
			tr::lng_tgs_icons_c1(tr::now),
			u"tristegram_tiles_c1"_q,
			QColor(0xF0, 0x7A, 0x0C));
		AddColorRow(
			group,
			controller,
			tr::lng_tgs_icons_c2(tr::now),
			u"tristegram_tiles_c2"_q,
			QColor(0xE8, 0x31, 0x8A));
	}
	AddNote(content, tr::lng_tgs_tiles_note(tr::now));
}

void FillChats(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	AddLabel(content, tr::lng_tgc_title(tr::now));
	{
		const auto group = AddGroup(content);
		AddActionRow(
			group,
			tr::lng_tgc_open(tr::now),
			nullptr,
			[=] {
				controller->hideLayer(anim::type::instant);
				ShowChatCleaner(controller);
			},
			true);
		AddNote(content, tr::lng_tgc_about(tr::now));
	}
	AddLabel(content, tr::lng_tgs_time(tr::now));
	AddSegments(
		content,
		{ u"12:36"_q, u"12:36:13"_q },
		Extera::Bool(u"time_with_seconds"_q) ? 1 : 0,
		[](int index) {
			Config().setBool(u"time_with_seconds"_q, index == 1);
		});
	AddSegments(
		content,
		{ u"24h"_q, u"AM / PM"_q },
		Extera::Bool(u"time_12h"_q) ? 1 : 0,
		[](int index) {
			Config().setBool(u"time_12h"_q, index == 1);
		});
	AddLabel(content, tr::lng_tgs_names_title(tr::now));
	{
		const auto group = AddGroup(content);
		const auto texts = std::vector<QString>{
			tr::lng_tgs_names_telegram(tr::now),
			tr::lng_tgs_icons_accent(tr::now),
			tr::lng_tgs_icons_color(tr::now),
			tr::lng_tgs_icons_logo(tr::now),
			tr::lng_tgs_icons_gradient(tr::now),
		};
		const auto modeKey = u"tristegram_name_mode"_q;
		const auto title = tr::lng_tgs_names_title(tr::now);
		const auto current = [=] {
			const auto mode = Extera::Int(modeKey);
			return (!mode && Extera::Bool(u"name_accent"_q))
				? 1
				: std::clamp(mode, 0, int(texts.size()) - 1);
		};
		AddActionRow(
			group,
			tr::lng_tgs_names_style(tr::now),
			[=] { return texts[current()]; },
			[=] {
				controller->show(Box([=](not_null<Ui::GenericBox*> box) {
					SingleChoiceBox(box, {
						.title = rpl::single(title),
						.options = texts,
						.initialSelection = current(),
						.callback = [=](int index) {
							Config().setBool(u"name_accent"_q, false);
							Config().setInt(modeKey, index);
						},
					});
				}));
			});
		AddColorRow(
			group,
			controller,
			tr::lng_tgs_icons_c1(tr::now),
			u"tristegram_name_c1"_q,
			QColor(0xF0, 0x7A, 0x0C));
		AddColorRow(
			group,
			controller,
			tr::lng_tgs_icons_c2(tr::now),
			u"tristegram_name_c2"_q,
			QColor(0xE8, 0x31, 0x8A));
	}
	AddNote(content, tr::lng_tgs_names_note(tr::now));
	AddLabel(content, tr::lng_tgs_text_bubbles(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigSlider(
			group,
			tr::lng_tristegram_bubble_radius(tr::now),
			u"tristegram_bubble_radius"_q,
			Range(6, 26, 2),
			[](int value) { return QString::number(value) + u" px"_q; },
			false);
		AddConfigToggle(group, tr::lng_tgs_no_tails(tr::now), u"remove_message_tail"_q);
		AddConfigToggle(group, tr::lng_tristegram_compose_capsule(tr::now), u"tristegram_compose_capsule"_q);
		AddConfigToggle(group, tr::lng_tristegram_header_center(tr::now), u"tristegram_header_center"_q);
	}
	AddNote(content, tr::lng_tgs_radius_note(tr::now));
	AddLabel(content, tr::lng_tgs_tabs(tr::now));
	{
		const auto group = AddGroup(content);
		AddActionRow(
			group,
			tr::lng_tristegram_tabs_create(tr::now),
			nullptr,
			[=] { CreateTypeTabs(controller); },
			true);
		AddActionRow(
			group,
			tr::lng_tgs_folders(tr::now),
			nullptr,
			[=] { OpenNative(controller, Settings::FoldersId()); });
		AddActionRow(
			group,
			tr::lng_tgs_chat_settings(tr::now),
			nullptr,
			[=] { OpenNative(controller, Settings::ChatId(), true); });
	}
	AddNote(content, tr::lng_tristegram_tabs_about(tr::now));
}

void FillPrivacy(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tgs_hide_phone(tr::now), u"hide_phone"_q);
	}
	AddLabel(content, tr::lng_tgs_hide_mode(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tgs_hide_mode(tr::now), u"streamer_mode"_q);
	}
	AddNote(content, tr::lng_tgs_hide_note(tr::now));
	AddLabel(content, tr::lng_tgs_hide_what(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tgs_hide_names(tr::now), u"streamer_private_name"_q);
		AddConfigToggle(group, tr::lng_tgs_hide_avatars(tr::now), u"streamer_private_avatar"_q);
		AddConfigToggle(group, tr::lng_tgs_hide_texts(tr::now), u"streamer_private_text"_q);
		AddConfigToggle(group, tr::lng_tgs_hide_everywhere(tr::now), u"streamer_everywhere"_q);
	}
	{
		const auto group = AddGroup(content);
		AddActionRow(
			group,
			tr::lng_tgs_hide_more(tr::now),
			nullptr,
			[=] { OpenNative(controller, Settings::ExteraPrivacyId()); });
		AddActionRow(
			group,
			tr::lng_tgs_tg_privacy(tr::now),
			nullptr,
			[=] { OpenNative(controller, Settings::PrivacySecurityId()); });
	}
}

void FillEco(not_null<Ui::VerticalLayout*> content) {
	{
		const auto group = AddGroup(content);
		AddConfigToggle(group, tr::lng_tristegram_eco(tr::now), u"tristegram_eco"_q);
	}
	AddNote(content, tr::lng_tgs_eco_note(tr::now));

	// Tristegram: screen recording of the Telegram window.
	AddLabel(content, tr::lng_tgr_title(tr::now));
	{
		const auto group = AddGroup(content);
		AddConfigToggle(
			group,
			tr::lng_tgr_button(tr::now),
			u"tristegram_screen_record"_q);
	}
	AddLabel(content, tr::lng_tgr_fps(tr::now));
	{
		const auto values = std::vector<int>{ 15, 24, 30, 60 };
		const auto current = Extera::Int(u"tristegram_record_fps"_q);
		const auto i = std::find(values.begin(), values.end(), current);
		AddSegments(
			content,
			{ u"15"_q, u"24"_q, u"30"_q, u"60"_q },
			(i == values.end()) ? 1 : int(i - values.begin()),
			[=](int index) {
				Config().setInt(u"tristegram_record_fps"_q, values[index]);
			});
	}
	AddLabel(content, tr::lng_tgr_quality(tr::now));
	{
		const auto values = std::vector<int>{ 50, 75, 100 };
		const auto current = Extera::Int(u"tristegram_record_scale"_q);
		const auto i = std::find(values.begin(), values.end(), current);
		AddSegments(
			content,
			{
				tr::lng_tgr_quality_low(tr::now),
				tr::lng_tgr_quality_mid(tr::now),
				tr::lng_tgr_quality_high(tr::now),
			},
			(i == values.end()) ? 1 : int(i - values.begin()),
			[=](int index) {
				Config().setInt(u"tristegram_record_scale"_q, values[index]);
			});
	}
	AddNote(content, tr::lng_tgr_note(tr::now));
}

void FillAccounts(
		not_null<Ui::VerticalLayout*> content,
		not_null<Window::SessionController*> controller) {
	{
		const auto group = AddGroup(content);
		AddActionRow(
			group,
			tr::lng_tgs_profile(tr::now),
			nullptr,
			[=] { OpenNative(controller, Settings::InformationId()); });
		AddActionRow(
			group,
			tr::lng_tgs_devices(tr::now),
			nullptr,
			[=] { OpenNative(controller, Settings::SessionsId()); });
	}
	AddNote(content, tr::lng_tgs_accounts_note(tr::now));
	AddLabel(content, tr::lng_tgs_reset(tr::now));
	{
		const auto group = AddGroup(content);
		AddActionRow(
			group,
			tr::lng_tgs_reset_all(tr::now),
			nullptr,
			[=] {
				controller->show(Ui::MakeConfirmBox({
					.text = tr::lng_extera_reset_sure(),
					.confirmed = [=](Fn<void()> close) {
						Config().reset();
						close();
					},
					.confirmText = tr::lng_extera_reset(),
					.confirmStyle = &st::attentionBoxButton,
				}));
			});
	}
}

struct NavItem {
	QString title;
	const style::icon *icon = nullptr;
	QColor color;
	std::optional<Section> section;
	Fn<void()> action;
	bool groupStart = false;
};

class SettingsWindow final : public Ui::RpWidget {
public:
	SettingsWindow(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> close,
		int height);

	void showSection(Section section);

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void setupNav();
	void paintNav(QPainter &p);
	void refillPane();
	[[nodiscard]] int navItemTop(int index) const;
	[[nodiscard]] int navIndexAt(QPoint point) const;
	[[nodiscard]] QString sectionTitle(Section section) const;
	void openNative(Fn<void()> show);

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _close;
	const int _height = 0;
	std::vector<NavItem> _items;
	Section _section = Section::Look;
	int _navOver = -1;

	Ui::ScrollArea *_navScroll = nullptr;
	Ui::RpWidget *_nav = nullptr;
	Ui::RpWidget *_header = nullptr;
	Ui::IconButton *_closeButton = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	Ui::VerticalLayout *_content = nullptr;

};

SettingsWindow::SettingsWindow(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> close,
	int height)
: RpWidget(parent)
, _controller(controller)
, _close(std::move(close))
, _height(height)
, _section(Section(std::clamp(
	Extera::Int(u"tristegram_settings_section"_q),
	0,
	int(Section::Accounts)))) {
	const auto item = [&](
			QString title,
			const style::icon &icon,
			QColor color,
			Section section) {
		_items.push_back({
			.title = std::move(title),
			.icon = &icon,
			.color = color,
			.section = section,
		});
	};
	item(tr::lng_tgs_s_look(tr::now), st::menuIconPalette, QColor(0xF0, 0x7A, 0x0C), Section::Look);
	item(tr::lng_tgs_s_logo(tr::now), st::menuIconDraw, QColor(0xFF, 0x5E, 0x3A), Section::Logo);
	item(tr::lng_tgs_s_fonts(tr::now), st::menuIconFont, QColor(0x8E, 0x6C, 0xEF), Section::Fonts);
	item(tr::lng_tgs_s_themes(tr::now), st::menuIconChangeColors, QColor(0xE8, 0x50, 0x8A), Section::Themes);
	item(tr::lng_tgs_s_bg(tr::now), st::menuIconPhoto, QColor(0x2F, 0xB4, 0xF0), Section::Background);
	item(tr::lng_tgs_s_anim(tr::now), st::menuIconStar, QColor(0xFF, 0xB2, 0x1F), Section::Animations);
	item(tr::lng_tgs_s_avatars(tr::now), st::menuIconProfile, QColor(0x22, 0xC5, 0x8B), Section::Avatars);
	item(tr::lng_tgs_s_icons(tr::now), st::menuIconEmoji, QColor(0x2F, 0x8C, 0xF0), Section::Icons);
	item(tr::lng_tgs_s_chats(tr::now), st::menuIconChatBubble, QColor(0x3A, 0x9B, 0xF0), Section::Chats);
	item(tr::lng_tgs_s_privacy(tr::now), st::menuIconStealth, QColor(0x5B, 0x6B, 0xE8), Section::Privacy);
	item(tr::lng_tgs_s_eco(tr::now), st::menuIconPowerUsage, QColor(0x3C, 0xB3, 0x71), Section::Eco);
	item(tr::lng_tgs_s_accounts(tr::now), st::menuIconGroups, QColor(0x26, 0xB0, 0xA2), Section::Accounts);

	const auto native = [&](
			QString title,
			const style::icon &icon,
			QColor color,
			Fn<void()> show,
			bool groupStart = false) {
		_items.push_back({
			.title = std::move(title),
			.icon = &icon,
			.color = color,
			.action = [this, show] { openNative(show); },
			.groupStart = groupStart,
		});
	};
	const auto controllerPtr = _controller;
	const auto open = [=](Settings::Type type) {
		return [=] {
			BypassOnce = true;
			controllerPtr->showSettings(type);
			BypassOnce = false;
		};
	};
	native(tr::lng_tgs_n_notifications(tr::now), st::menuIconNotifications, QColor(0xF2, 0x54, 0x5B), open(Settings::NotificationsId()), true);
	native(tr::lng_tgs_n_privacy(tr::now), st::menuIconLock, QColor(0x5B, 0x6B, 0xE8), open(Settings::PrivacySecurityId()));
	native(tr::lng_tgs_n_folders(tr::now), st::menuIconAddToFolder, QColor(0x2F, 0x8C, 0xF0), open(Settings::FoldersId()));
	native(tr::lng_tgs_n_advanced(tr::now), st::menuIconManage, QColor(0x8E, 0x8E, 0x93), open(Settings::AdvancedId()));
	native(tr::lng_tgs_n_devices(tr::now), st::menuIconDevices, QColor(0x3C, 0xB3, 0x71), open(Settings::SessionsId()));
	native(tr::lng_tgs_n_plugins(tr::now), st::exteraIconPlugins, QColor(0xE2, 0x55, 0x7B), open(Settings::ExteraPluginsId()), true);
	native(tr::lng_tgs_n_features(tr::now), st::exteraIconMore, QColor(0xF0, 0x7A, 0x0C), open(Settings::ExteraId()));
	native(tr::lng_tgs_n_classic(tr::now), st::menuIconSettings, QColor(0x8E, 0x8E, 0x93), [=] {
		BypassOnce = true;
		controllerPtr->showSettings(Settings::MainId());
		BypassOnce = false;
	});

	setupNav();

	_header = Ui::CreateChild<Ui::RpWidget>(this);
	_header->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_header);
		p.setFont(st::boxTitle.style.font);
		p.setPen(st::windowFg);
		p.drawText(
			_header->rect().marginsRemoved(QMargins(Px(20), 0, Px(60), 0)),
			Qt::AlignLeft | Qt::AlignVCenter,
			sectionTitle(_section));
	}, _header->lifetime());
	_closeButton = Ui::CreateChild<Ui::IconButton>(
		_header,
		st::boxTitleClose);
	_closeButton->setClickedCallback([=] { _close(); });

	_scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll);
	refillPane();

	paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(this);
		const auto navWidth = _navScroll->width();
		p.fillRect(
			QRect(0, 0, navWidth, this->height()),
			Alpha(st::windowFg->c, 0.025));
		p.fillRect(
			QRect(navWidth, 0, st::lineWidth, this->height()),
			Alpha(st::windowFg->c, 0.08));
	}, lifetime());

	sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto navWidth = Px(224);
		_navScroll->setGeometry(0, 0, navWidth, size.height());
		_nav->resize(navWidth, _nav->height());
		const auto headerHeight = Px(60);
		_header->setGeometry(
			navWidth,
			0,
			size.width() - navWidth,
			headerHeight);
		_closeButton->moveToRight(Px(10), (headerHeight - _closeButton->height()) / 2);
		_scroll->setGeometry(
			navWidth + st::lineWidth,
			headerHeight,
			size.width() - navWidth - st::lineWidth,
			size.height() - headerHeight);
		if (_content) {
			_content->resizeToWidth(_scroll->width());
		}
	}, lifetime());

	Config().changes(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"tristegram_logo"_q);
	}) | rpl::on_next([=] {
		_nav->update();
	}, lifetime());
}

int SettingsWindow::resizeGetHeight(int newWidth) {
	return _height;
}

QString SettingsWindow::sectionTitle(Section section) const {
	for (const auto &item : _items) {
		if (item.section && *item.section == section) {
			return item.title;
		}
	}
	return QString();
}

void SettingsWindow::openNative(Fn<void()> show) {
	_close();
	show();
}

int SettingsWindow::navItemTop(int index) const {
	auto top = Px(64);
	for (auto i = 0; i != index; ++i) {
		top += Px(42);
		if (_items[i + 1].groupStart) {
			top += Px(26);
		}
	}
	return top;
}

int SettingsWindow::navIndexAt(QPoint point) const {
	for (auto i = 0; i != int(_items.size()); ++i) {
		const auto top = navItemTop(i);
		if (point.y() >= top && point.y() < top + Px(40)) {
			return i;
		}
	}
	return -1;
}

void SettingsWindow::setupNav() {
	_navScroll = Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll);
	auto nav = object_ptr<Ui::RpWidget>(_navScroll);
	_nav = nav.data();
	const auto count = int(_items.size());
	_nav->resize(Px(224), navItemTop(count - 1) + Px(40) + Px(14));
	_nav->setMouseTracking(true);
	_nav->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_nav);
		paintNav(p);
	}, _nav->lifetime());
	_nav->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseMove) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			const auto index = navIndexAt(mouse->pos());
			if (_navOver != index) {
				_navOver = index;
				_nav->setCursor((index >= 0)
					? style::cur_pointer
					: style::cur_default);
				_nav->update();
			}
		} else if (type == QEvent::Leave) {
			_navOver = -1;
			_nav->update();
		} else if (type == QEvent::MouseButtonRelease) {
			const auto mouse = static_cast<QMouseEvent*>(e.get());
			const auto index = navIndexAt(mouse->pos());
			if (index < 0) {
				return;
			}
			const auto &item = _items[index];
			if (item.section) {
				showSection(*item.section);
			} else if (item.action) {
				item.action();
			}
		}
	}, _nav->lifetime());
	_navScroll->setOwnedWidget(std::move(nav));
}

void SettingsWindow::paintNav(QPainter &p) {
	auto hq = PainterHighQualityEnabler(p);
	const auto width = _nav->width();
	const auto style = CurrentLogoStyle();

	// Header: the logo and "Settings".
	const auto logo = Px(30);
	PaintLogo(
		p,
		QRectF(Px(16), (Px(60) - logo) / 2., logo, logo),
		[&] { auto copy = style; copy.glow = false; return copy; }());
	p.setFont(st::boxTitle.style.font);
	p.setPen(st::windowFg);
	p.drawText(
		QRect(Px(16) + logo + Px(10), 0, width - logo - Px(30), Px(60)),
		Qt::AlignLeft | Qt::AlignVCenter,
		tr::lng_tgs_settings(tr::now));

	for (auto i = 0; i != int(_items.size()); ++i) {
		const auto &item = _items[i];
		const auto top = navItemTop(i);
		if (item.groupStart) {
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			const auto caption = (i == int(_items.size()) - 3)
				? u"exDesktop"_q
				: u"Telegram"_q;
			p.drawText(
				QRect(Px(22), top - Px(26), width - Px(44), Px(24)),
				Qt::AlignLeft | Qt::AlignVCenter,
				caption.toUpper());
		}
		const auto row = QRectF(Px(8), top, width - Px(16), Px(40));
		const auto active = item.section && (*item.section == _section);
		if (active) {
			for (auto g = 3; g != 0; --g) {
				p.setPen(Qt::NoPen);
				p.setBrush(Alpha(style.from, 0.06 * style.glowPower));
				const auto spread = g * 2.;
				p.drawRoundedRect(
					row.marginsAdded(QMarginsF(spread, spread, spread, spread)),
					Px(12) + spread,
					Px(12) + spread);
			}
			auto fill = QLinearGradient(row.topLeft(), row.bottomRight());
			fill.setColorAt(0., style.from);
			fill.setColorAt(1., style.to);
			p.setBrush(fill);
			p.drawRoundedRect(row, Px(12), Px(12));
			auto sheen = QLinearGradient(row.topLeft(), row.bottomLeft());
			sheen.setColorAt(0., QColor(255, 255, 255, 50));
			sheen.setColorAt(0.6, QColor(255, 255, 255, 0));
			p.setBrush(sheen);
			p.drawRoundedRect(row, Px(12), Px(12));
		} else if (i == _navOver) {
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(st::windowFg->c, 0.06));
			p.drawRoundedRect(row, Px(12), Px(12));
		}
		const auto tileSize = Px(28);
		const auto tile = QRectF(
			row.x() + Px(8),
			row.y() + (row.height() - tileSize) / 2.,
			tileSize,
			tileSize);
		p.setPen(Qt::NoPen);
		p.setBrush(active
			? QColor(255, 255, 255, 60)
			: Alpha(item.color, 0.18));
		p.drawRoundedRect(tile, Px(8), Px(8));
		if (item.icon) {
			const auto icon = item.icon;
			const auto scale = 0.75;
			p.save();
			p.translate(tile.center());
			p.scale(scale, scale);
			icon->paint(
				p,
				QPoint(-icon->width() / 2, -icon->height() / 2),
				width,
				active ? QColor(255, 255, 255) : item.color);
			p.restore();
		}
		p.setFont(active ? st::semiboldFont : st::normalFont);
		p.setPen(active ? QColor(255, 255, 255) : st::windowFg->c);
		const auto textLeft = int(tile.right()) + Px(12);
		const auto text = QRect(
			textLeft,
			int(row.y()),
			int(row.right()) - textLeft - Px(8),
			int(row.height()));
		p.drawText(
			text,
			Qt::AlignLeft | Qt::AlignVCenter,
			st::normalFont->elided(item.title, text.width()));
	}
}

void SettingsWindow::showSection(Section section) {
	if (_section == section) {
		return;
	}
	_section = section;
	Config().setInt(u"tristegram_settings_section"_q, int(section));
	_nav->update();
	_header->update();
	refillPane();
}

void SettingsWindow::refillPane() {
	auto content = object_ptr<Ui::VerticalLayout>(_scroll);
	const auto raw = content.data();
	const auto inner = raw->add(
		object_ptr<Ui::VerticalLayout>(raw),
		QMargins(Px(16), 0, Px(16), Px(16)));
	const auto go = [=](Section section) {
		crl::on_main(this, [=] { showSection(section); });
	};
	switch (_section) {
	case Section::Look: FillLook(inner, _controller, go); break;
	case Section::Logo: FillLogo(inner, _controller); break;
	case Section::Fonts: FillFonts(inner, _controller); break;
	case Section::Themes: FillThemes(inner, _controller); break;
	case Section::Background: FillBackground(inner, _controller); break;
	case Section::Animations: FillAnimations(inner); break;
	case Section::Avatars: FillAvatars(inner); break;
	case Section::Icons: FillIcons(inner, _controller); break;
	case Section::Chats: FillChats(inner, _controller); break;
	case Section::Privacy: FillPrivacy(inner, _controller); break;
	case Section::Eco: FillEco(inner); break;
	case Section::Accounts: FillAccounts(inner, _controller); break;
	}
	_content = raw;
	_scroll->setOwnedWidget(std::move(content));
	_content->resizeToWidth(_scroll->width());
	_scroll->scrollToY(0);
}

} // namespace

void ShowSettingsWindow(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto window = controller->widget();
		const auto width = std::min(Px(820), window->width() - Px(40));
		const auto height = std::min(Px(660), window->height() - Px(60));
		box->setNoContentMargin(true);
		box->setWidth(width);
		box->addRow(
			object_ptr<SettingsWindow>(
				box,
				controller,
				[=] { box->closeBox(); },
				height),
			QMargins());
	}));
}

bool InterceptSettings(
		not_null<Window::SessionController*> controller,
		const Settings::Type &type) {
	if (BypassOnce || !Extera::Bool(u"tristegram_settings_window"_q)) {
		return false;
	} else if (type == Settings::MainId()
		|| type == Settings::ExteraId()) {
		// The big exDesktop page with tiles is replaced by our window too,
		// it stays reachable from the window sidebar.
		ShowSettingsWindow(controller);
		return true;
	} else if (type == Settings::ChatId()) {
		// The classic Chat Settings page with themes and accent colors
		// is replaced by our "Themes" section.
		Config().setInt(
			u"tristegram_settings_section"_q,
			int(Section::Themes));
		ShowSettingsWindow(controller);
		return true;
	}
	return false;
}

} // namespace Tristegram
