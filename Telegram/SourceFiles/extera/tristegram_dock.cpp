/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_dock.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_pinned_list.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"

#include <QtGui/QCursor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>

#include <cmath>

namespace Tristegram {
namespace {

constexpr auto kMaxChats = 8;
constexpr auto kMaxFolders = 6;

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(alpha);
	return color;
}

class Dock final : public Ui::RpWidget {
public:
	Dock(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	struct Item {
		FilterId filter = 0;
		bool isFilter = false;
		PeerData *peer = nullptr;
		QString title;
		mutable Ui::PeerUserpicView userpic;
	};

	void refreshItems();
	void relayout();
	void checkMouse(QPoint global);
	void setShown(bool shown);
	[[nodiscard]] double scaleFor(int index) const;
	[[nodiscard]] QRectF itemRect(int index) const;
	[[nodiscard]] int itemAt(QPoint position) const;

	const not_null<QWidget*> _parent;
	const not_null<Window::SessionController*> _controller;
	std::vector<Item> _items;
	Ui::Animations::Simple _shownAnimation;
	Ui::Animations::Simple _hoverAnimation;
	base::Timer _hideTimer;
	bool _shown = false;
	double _mouseX = -1.;
	int _over = -1;

};

Dock::Dock(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _parent(parent)
, _controller(controller)
, _hideTimer([=] { setShown(false); }) {
	setMouseTracking(true);
	setAttribute(Qt::WA_TranslucentBackground);
	hide();

	const auto session = &controller->session();
	session->data().chatsFilters().changed(
	) | rpl::on_next([=] {
		refreshItems();
	}, lifetime());
	refreshItems();

	base::install_event_filter(this, _parent, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Resize) {
			relayout();
		}
		return base::EventFilterResult::Continue;
	});
	// The parent's children get the mouse, so look at all moves.
	base::install_event_filter(this, qApp, [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::MouseMove) {
			checkMouse(QCursor::pos());
		}
		return base::EventFilterResult::Continue;
	});
	Extera::Config::Instance().boolChanges(
		u"tristegram_dock"_q
	) | rpl::on_next([=](bool enabled) {
		if (!enabled) {
			setShown(false);
		}
	}, lifetime());
}

void Dock::refreshItems() {
	auto items = std::vector<Item>();
	const auto session = &_controller->session();
	auto folders = 0;
	for (const auto &filter : session->data().chatsFilters().list()) {
		if (folders++ >= kMaxFolders) {
			break;
		}
		items.push_back({
			.filter = filter.id(),
			.isFilter = true,
			.title = filter.id()
				? filter.titleText().text
				: tr::lng_filters_all(tr::now),
		});
	}
	auto chats = 0;
	const auto &pinned = session->data().chatsList()->pinned()->order();
	for (const auto &key : pinned) {
		if (chats >= kMaxChats) {
			break;
		}
		if (const auto peer = key.peer()) {
			items.push_back({
				.peer = peer,
				.title = peer->name(),
			});
			++chats;
		}
	}
	_items = std::move(items);
	relayout();
	update();
}

void Dock::relayout() {
	const auto count = std::max(int(_items.size()), 1);
	const auto base = Px(44);
	const auto gap = Px(10);
	const auto w = count * base + (count - 1) * gap + Px(28);
	const auto h = Px(44) + Px(24) + Px(26);
	const auto shown = _shownAnimation.value(_shown ? 1. : 0.);
	const auto bottom = Px(14);
	const auto y = _parent->height() - int(std::round(h * shown)) - bottom + int(std::round(bottom * (1. - shown)));
	setGeometry(
		(_parent->width() - w) / 2,
		y,
		w,
		h);
}

void Dock::checkMouse(QPoint global) {
	if (!Extera::Bool(u"tristegram_dock"_q)
		|| !_parent->isVisible()
		|| _items.empty()) {
		return;
	}
	const auto local = _parent->mapFromGlobal(global);
	const auto zone = QRect(
		0,
		_parent->height() - Px(16),
		_parent->width(),
		Px(16));
	const auto inside = _parent->rect().contains(local);
	if (inside && zone.contains(local)) {
		_hideTimer.cancel();
		setShown(true);
	} else if (_shown) {
		const auto mine = geometry().marginsAdded({ 20, 30, 20, 10 });
		if (mine.contains(local)) {
			_hideTimer.cancel();
		} else if (!_hideTimer.isActive()) {
			_hideTimer.callOnce(500);
		}
	}
}

void Dock::setShown(bool shown) {
	if (_shown == shown) {
		return;
	}
	_shown = shown;
	if (shown) {
		refreshItems();
		show();
		raise();
	}
	_shownAnimation.start([=] {
		relayout();
		if (!_shown && !_shownAnimation.animating()) {
			hide();
		}
	}, shown ? 0. : 1., shown ? 1. : 0., 220, anim::easeOutCirc);
	relayout();
}

double Dock::scaleFor(int index) const {
	if (_mouseX < 0.) {
		return 1.;
	}
	const auto base = double(Px(44));
	const auto gap = double(Px(10));
	const auto center = Px(14) + index * (base + gap) + base / 2.;
	const auto distance = std::abs(center - _mouseX) / (base * 1.6);
	const auto hover = _hoverAnimation.value(1.);
	return 1. + 0.45 * hover * std::max(0., 1. - distance * distance);
}

QRectF Dock::itemRect(int index) const {
	const auto base = double(Px(44));
	const auto gap = double(Px(10));
	const auto scale = scaleFor(index);
	const auto size = base * scale;
	const auto center = Px(14) + index * (base + gap) + base / 2.;
	const auto bottom = height() - Px(12);
	return QRectF(center - size / 2., bottom - size, size, size);
}

int Dock::itemAt(QPoint position) const {
	for (auto i = 0; i != int(_items.size()); ++i) {
		if (itemRect(i).contains(position)) {
			return i;
		}
	}
	return -1;
}

void Dock::mouseMoveEvent(QMouseEvent *e) {
	if (_mouseX < 0.) {
		_hoverAnimation.start([=] { update(); }, 0., 1., 150);
	}
	_mouseX = e->pos().x();
	const auto over = itemAt(e->pos());
	if (_over != over) {
		_over = over;
		setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
	}
	update();
}

void Dock::leaveEventHook(QEvent *e) {
	_mouseX = -1.;
	_over = -1;
	update();
}

void Dock::mouseReleaseEvent(QMouseEvent *e) {
	const auto index = itemAt(e->pos());
	if (index < 0 || e->button() != Qt::LeftButton) {
		return;
	}
	const auto &item = _items[index];
	if (item.isFilter) {
		_controller->setActiveChatsFilter(item.filter);
	} else if (item.peer) {
		_controller->showPeerHistory(
			item.peer,
			Window::SectionShow::Way::ClearStack);
	}
	setShown(false);
}

void Dock::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto shelf = QRectF(
		Px(4),
		height() - Px(12) - Px(44) - Px(8),
		width() - Px(8),
		Px(44) + Px(16));
	p.setPen(QPen(Alpha(st::windowFg->c, 0.12), 1.));
	auto glass = st::windowBg->c;
	glass.setAlphaF(0.88);
	p.setBrush(glass);
	p.drawRoundedRect(shelf, Px(20), Px(20));
	auto sheen = QLinearGradient(shelf.topLeft(), shelf.bottomLeft());
	sheen.setColorAt(0., QColor(255, 255, 255, 28));
	sheen.setColorAt(1., QColor(255, 255, 255, 0));
	p.setPen(Qt::NoPen);
	p.setBrush(sheen);
	p.drawRoundedRect(shelf, Px(20), Px(20));

	const auto active = _controller->activeChatsFilterCurrent();
	for (auto i = 0; i != int(_items.size()); ++i) {
		const auto &item = _items[i];
		const auto r = itemRect(i);
		if (item.isFilter) {
			auto fill = QLinearGradient(r.topLeft(), r.bottomRight());
			fill.setColorAt(0., colors.from);
			fill.setColorAt(1., colors.to);
			p.setBrush(fill);
			p.drawRoundedRect(r, r.width() * 0.28, r.width() * 0.28);
			p.setPen(QColor(255, 255, 255));
			auto font = st::semiboldFont->f;
			font.setPixelSize(std::max(int(r.height() * 0.38), 8));
			p.setFont(font);
			const auto letter = item.title.isEmpty()
				? QString()
				: item.title.left(1).toUpper();
			p.drawText(r, Qt::AlignCenter, letter);
			p.setPen(Qt::NoPen);
			if (item.filter == active) {
				p.setBrush(st::windowFg);
				p.drawEllipse(QPointF(r.center().x(), height() - Px(5)), Px(2), Px(2));
			}
		} else if (item.peer) {
			const auto size = int(std::round(r.width()));
			item.peer->paintUserpic(
				p,
				item.userpic,
				int(std::round(r.x())),
				int(std::round(r.y())),
				size);
		}
	}
	// The name of the item under the mouse above it.
	if (_over >= 0 && _over < int(_items.size())) {
		const auto &item = _items[_over];
		const auto r = itemRect(_over);
		const auto font = st::normalFont;
		const auto text = font->elided(item.title, Px(200));
		const auto w = font->width(text) + Px(16);
		const auto label = QRectF(
			std::clamp(r.center().x() - w / 2., 0., double(width() - w)),
			0,
			w,
			Px(22));
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.85));
		p.drawRoundedRect(label, Px(11), Px(11));
		p.setFont(font);
		p.setPen(st::windowBg);
		p.drawText(label, Qt::AlignCenter, text);
	}
}

} // namespace

void InstallDock(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller) {
	Ui::CreateChild<Dock>(parent.get(), controller);
}

} // namespace Tristegram
