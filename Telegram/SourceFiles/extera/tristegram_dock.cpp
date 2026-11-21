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
#include "data/data_thread.h"
#include "dialogs/dialogs_key.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_pinned_list.h"
#include "extera/extera_config.h"
#include "extera/tristegram_activity_ui.h"
#include "extera/tristegram_downloads.h"
#include "extera/tristegram_logo.h"
#include "extera/tristegram_music.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/effects/animations.h"
#include "ui/empty_userpic.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtGui/QCursor>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>
#include <QtGui/QPolygonF>
#include <QtWidgets/QApplication>

#include <cmath>

namespace Tristegram {
namespace {

constexpr auto kMaxChats = 8;
constexpr auto kMaxFolders = 6;
constexpr auto kMaxItems = 16;

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

enum class Side {
	Bottom,
	Left,
	Right,
};

[[nodiscard]] Side DockSide() {
	switch (Extera::Int(u"tristegram_dock_side"_q)) {
	case 1: return Side::Left;
	case 2: return Side::Right;
	}
	return Side::Bottom;
}

[[nodiscard]] int DockSize() {
	return Px(std::clamp(Extera::Int(u"tristegram_dock_size"_q), 32, 72));
}

[[nodiscard]] double DockMagnify() {
	return std::clamp(Extera::Int(u"tristegram_dock_magnify"_q), 0, 80) / 100.;
}

[[nodiscard]] double DockOpacity() {
	return std::clamp(Extera::Int(u"tristegram_dock_opacity"_q), 30, 100) / 100.;
}

[[nodiscard]] bool DockAlways() {
	return Extera::Int(u"tristegram_dock_mode"_q) == 1;
}

// "f:<filter>", "p:<peer>" and "b:<button>", empty for the default:
// the folders and the pinned chats.
[[nodiscard]] std::vector<QString> SavedIds() {
	auto result = std::vector<QString>();
	const auto array = QJsonDocument::fromJson(
		Extera::String(u"tristegram_dock_items"_q).toUtf8()).array();
	for (const auto &value : array) {
		const auto id = value.toString();
		if (id.size() > 2 && id[1] == ':') {
			result.push_back(id);
		}
	}
	return result;
}

void SaveIds(const std::vector<QString> &ids) {
	auto array = QJsonArray();
	for (const auto &id : ids) {
		array.push_back(id);
	}
	Extera::Config::Instance().setString(
		u"tristegram_dock_items"_q,
		ids.empty()
			? QString()
			: QString::fromUtf8(
				QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

[[nodiscard]] std::vector<QString> DefaultIds(
		not_null<Main::Session*> session) {
	auto result = std::vector<QString>();
	auto folders = 0;
	for (const auto &filter : session->data().chatsFilters().list()) {
		if (folders++ >= kMaxFolders) {
			break;
		}
		result.push_back(u"f:"_q + QString::number(filter.id()));
	}
	auto chats = 0;
	const auto &pinned = session->data().chatsList()->pinned()->order();
	for (const auto &key : pinned) {
		if (chats >= kMaxChats) {
			break;
		}
		if (const auto peer = key.peer()) {
			result.push_back(u"p:"_q + QString::number(peer->id.value));
			++chats;
		}
	}
	return result;
}

[[nodiscard]] std::vector<QString> CurrentIds(
		not_null<Main::Session*> session) {
	auto saved = SavedIds();
	return saved.empty() ? DefaultIds(session) : saved;
}

[[nodiscard]] QStringList ButtonIds() {
	return {
		u"saved"_q,
		u"music"_q,
		u"activity"_q,
		u"downloads"_q,
		u"settings"_q,
	};
}

[[nodiscard]] QString ButtonTitle(const QString &id) {
	if (id == u"saved"_q) {
		return tr::lng_saved_messages(tr::now);
	} else if (id == u"music"_q) {
		return tr::lng_tgm_title(tr::now);
	} else if (id == u"activity"_q) {
		return tr::lng_tgac_title(tr::now);
	} else if (id == u"downloads"_q) {
		return tr::lng_tgk_dock_downloads(tr::now);
	} else if (id == u"settings"_q) {
		return tr::lng_menu_settings(tr::now);
	}
	return QString();
}

[[nodiscard]] const style::icon *ButtonIcon(const QString &id) {
	if (id == u"music"_q) {
		return &st::menuIconSoundOn;
	} else if (id == u"activity"_q) {
		return &st::menuIconStats;
	} else if (id == u"downloads"_q) {
		return &st::menuIconDownload;
	} else if (id == u"settings"_q) {
		return &st::menuIconSettings;
	}
	return nullptr;
}

struct Item {
	QString id;
	FilterId filter = 0;
	bool isFilter = false;
	PeerData *peer = nullptr;
	QString button;
	QString title;
	mutable Ui::PeerUserpicView userpic;
};

[[nodiscard]] std::optional<Item> ResolveItem(
		not_null<Main::Session*> session,
		const QString &id) {
	const auto type = id.left(2);
	const auto value = id.mid(2);
	if (type == u"f:"_q) {
		const auto filterId = FilterId(value.toInt());
		for (const auto &filter : session->data().chatsFilters().list()) {
			if (filter.id() == filterId) {
				return Item{
					.id = id,
					.filter = filterId,
					.isFilter = true,
					.title = filterId
						? filter.titleText().text
						: tr::lng_filters_all(tr::now),
				};
			}
		}
	} else if (type == u"p:"_q) {
		const auto peerId = PeerId(value.toULongLong());
		if (const auto peer = session->data().peerLoaded(peerId)) {
			return Item{
				.id = id,
				.peer = peer,
				.title = peer->name(),
			};
		}
	} else if (type == u"b:"_q && ButtonIds().contains(value)) {
		return Item{
			.id = id,
			.button = value,
			.title = ButtonTitle(value),
		};
	}
	return std::nullopt;
}

void PaintItem(
		QPainter &p,
		const Item &item,
		QRectF r,
		const LogoStyle &colors) {
	auto hq = PainterHighQualityEnabler(p);
	if (item.peer) {
		item.peer->paintUserpic(
			p,
			item.userpic,
			int(std::round(r.x())),
			int(std::round(r.y())),
			int(std::round(r.width())));
		return;
	}
	if (item.button == u"saved"_q) {
		Ui::EmptyUserpic::PaintSavedMessages(
			p,
			int(std::round(r.x())),
			int(std::round(r.y())),
			int(std::round(r.x() * 2 + r.width())),
			int(std::round(r.width())));
		return;
	}
	auto fill = QLinearGradient(r.topLeft(), r.bottomRight());
	fill.setColorAt(0., colors.from);
	fill.setColorAt(1., colors.to);
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawRoundedRect(r, r.width() * 0.28, r.width() * 0.28);
	if (const auto icon = ButtonIcon(item.button)) {
		const auto scale = r.width() / icon->width() * 0.55;
		p.save();
		p.translate(r.center());
		p.scale(scale, scale);
		icon->paint(
			p,
			-icon->width() / 2,
			-icon->height() / 2,
			icon->width(),
			QColor(255, 255, 255));
		p.restore();
		return;
	}
	p.setPen(QColor(255, 255, 255));
	auto font = st::semiboldFont->f;
	font.setPixelSize(std::max(int(r.height() * 0.38), 8));
	p.setFont(font);
	p.drawText(
		r,
		Qt::AlignCenter,
		item.title.isEmpty() ? QString() : item.title.left(1).toUpper());
}

void Activate(
		not_null<Window::SessionController*> controller,
		const Item &item) {
	if (item.isFilter) {
		controller->setActiveChatsFilter(item.filter);
	} else if (item.peer) {
		controller->showPeerHistory(
			item.peer,
			Window::SectionShow::Way::ClearStack);
	} else if (item.button == u"saved"_q) {
		controller->showPeerHistory(
			controller->session().userPeerId(),
			Window::SectionShow::Way::ClearStack);
	} else if (item.button == u"music"_q) {
		ShowMusicWindow(controller);
	} else if (item.button == u"activity"_q) {
		ShowActivityWindow(controller);
	} else if (item.button == u"downloads"_q) {
		ShowDownloadsWindow(controller);
	} else if (item.button == u"settings"_q) {
		controller->showSettings();
	}
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
	void refreshItems();
	void relayout();
	void checkMouse(QPoint global);
	void setShown(bool shown);
	[[nodiscard]] bool vertical() const;
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
	double _mouse = -1.;
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
	Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key.startsWith(u"tristegram_dock"_q);
	}) | rpl::on_next([=](const QString &) {
		refreshItems();
		if (!Extera::Bool(u"tristegram_dock"_q)) {
			setShown(false);
		} else if (DockAlways()) {
			setShown(true);
		} else if (_shown) {
			_hideTimer.callOnce(500);
		}
		relayout();
		update();
	}, lifetime());
	if (Extera::Bool(u"tristegram_dock"_q) && DockAlways()) {
		crl::on_main(this, [=] { setShown(true); });
	}
}

bool Dock::vertical() const {
	return DockSide() != Side::Bottom;
}

void Dock::refreshItems() {
	const auto session = &_controller->session();
	auto items = std::vector<Item>();
	for (const auto &id : CurrentIds(session)) {
		if (int(items.size()) >= kMaxItems) {
			break;
		}
		if (auto item = ResolveItem(session, id)) {
			items.push_back(std::move(*item));
		}
	}
	_items = std::move(items);
	relayout();
	update();
}

void Dock::relayout() {
	const auto count = std::max(int(_items.size()), 1);
	const auto base = DockSize();
	const auto gap = base / 4;
	const auto along = count * base + (count - 1) * gap + Px(28);
	// Room for the grown icons and the label.
	const auto across = base + Px(24) + int(base * DockMagnify()) + Px(10);
	const auto shown = _shownAnimation.value(_shown ? 1. : 0.);
	const auto edge = Px(14);
	const auto slide = int(std::round((across + edge) * (1. - shown)));
	switch (DockSide()) {
	case Side::Bottom:
		setGeometry(
			(_parent->width() - along) / 2,
			_parent->height() - across - edge + slide,
			along,
			across);
		break;
	case Side::Left:
		setGeometry(
			edge - slide,
			(_parent->height() - along) / 2,
			across,
			along);
		break;
	case Side::Right:
		setGeometry(
			_parent->width() - across - edge + slide,
			(_parent->height() - along) / 2,
			across,
			along);
		break;
	}
}

void Dock::checkMouse(QPoint global) {
	if (!Extera::Bool(u"tristegram_dock"_q)
		|| !_parent->isVisible()
		|| _items.empty()
		|| DockAlways()) {
		return;
	}
	const auto local = _parent->mapFromGlobal(global);
	const auto strip = Px(16);
	const auto zone = [&] {
		switch (DockSide()) {
		case Side::Left:
			return QRect(0, 0, strip, _parent->height());
		case Side::Right:
			return QRect(_parent->width() - strip, 0, strip, _parent->height());
		case Side::Bottom:
			break;
		}
		return QRect(0, _parent->height() - strip, _parent->width(), strip);
	}();
	const auto inside = _parent->rect().contains(local);
	if (inside && zone.contains(local)) {
		_hideTimer.cancel();
		setShown(true);
	} else if (_shown) {
		const auto mine = geometry().marginsAdded({ 20, 30, 20, 20 });
		if (mine.contains(local)) {
			_hideTimer.cancel();
		} else if (!_hideTimer.isActive()) {
			_hideTimer.callOnce(500);
		}
	}
}

void Dock::setShown(bool shown) {
	if (_shown == shown) {
		if (shown) {
			raise();
		}
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
	if (_mouse < 0.) {
		return 1.;
	}
	const auto base = double(DockSize());
	const auto gap = base / 4.;
	const auto center = Px(14) + index * (base + gap) + base / 2.;
	const auto distance = std::abs(center - _mouse) / (base * 1.6);
	const auto hover = _hoverAnimation.value(1.);
	return 1. + DockMagnify() * hover * std::max(0., 1. - distance * distance);
}

QRectF Dock::itemRect(int index) const {
	const auto base = double(DockSize());
	const auto gap = base / 4.;
	const auto size = base * scaleFor(index);
	const auto center = Px(14) + index * (base + gap) + base / 2.;
	switch (DockSide()) {
	case Side::Left: {
		const auto left = Px(12);
		return QRectF(left, center - size / 2., size, size);
	}
	case Side::Right: {
		const auto right = width() - Px(12);
		return QRectF(right - size, center - size / 2., size, size);
	}
	case Side::Bottom:
		break;
	}
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
	if (_mouse < 0.) {
		_hoverAnimation.start([=] { update(); }, 0., 1., 150);
	}
	_mouse = vertical() ? e->pos().y() : e->pos().x();
	const auto over = itemAt(e->pos());
	if (_over != over) {
		_over = over;
		setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
	}
	update();
}

void Dock::leaveEventHook(QEvent *e) {
	_mouse = -1.;
	_over = -1;
	update();
}

void Dock::mouseReleaseEvent(QMouseEvent *e) {
	const auto index = itemAt(e->pos());
	if (index < 0 || e->button() != Qt::LeftButton) {
		return;
	}
	const auto item = _items[index].id;
	const auto session = &_controller->session();
	if (const auto resolved = ResolveItem(session, item)) {
		Activate(_controller, *resolved);
	}
	if (!DockAlways()) {
		setShown(false);
	}
}

void Dock::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto base = DockSize();
	const auto shelfDepth = base + Px(16);
	const auto shelf = [&] {
		switch (DockSide()) {
		case Side::Left:
			return QRectF(Px(4), Px(4), shelfDepth, height() - Px(8));
		case Side::Right:
			return QRectF(
				width() - Px(4) - shelfDepth,
				Px(4),
				shelfDepth,
				height() - Px(8));
		case Side::Bottom:
			break;
		}
		return QRectF(
			Px(4),
			height() - Px(12) - base - Px(8),
			width() - Px(8),
			shelfDepth);
	}();
	p.setPen(QPen(Alpha(st::windowFg->c, 0.12), 1.));
	p.setBrush(Alpha(st::windowBg->c, DockOpacity()));
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
		PaintItem(p, item, r, colors);
		if (item.isFilter && item.filter == active) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowFg);
			const auto dot = vertical()
				? QPointF(
					(DockSide() == Side::Left) ? Px(6) : (width() - Px(6)),
					r.center().y())
				: QPointF(r.center().x(), height() - Px(5));
			p.drawEllipse(dot, Px(2), Px(2));
		}
	}
	// The name of the item under the mouse next to it.
	if (_over >= 0 && _over < int(_items.size())) {
		const auto &item = _items[_over];
		const auto r = itemRect(_over);
		const auto font = st::normalFont;
		const auto text = font->elided(item.title, Px(200));
		const auto w = font->width(text) + Px(16);
		const auto h = Px(22);
		auto label = QRectF();
		if (vertical()) {
			// No room beside a side dock, show it above the icon.
			label = QRectF(
				std::clamp(r.center().x() - w / 2., 0., double(std::max(width() - w, 0))),
				std::max(r.y() - h - Px(2), 0.),
				std::min(double(w), double(width())),
				h);
		} else {
			label = QRectF(
				std::clamp(r.center().x() - w / 2., 0., double(width() - w)),
				0,
				w,
				h);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.85));
		p.drawRoundedRect(label, Px(11), Px(11));
		p.setFont(font);
		p.setPen(st::windowBg);
		p.drawText(label, Qt::AlignCenter, font->elided(text, int(label.width()) - Px(8)));
	}
}

// The list in the dock editor: an icon, a name, up, down and remove.
class EditorList final : public Ui::RpWidget {
public:
	EditorList(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void reload();

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	enum class Part {
		None,
		Up,
		Down,
		Remove,
	};
	[[nodiscard]] QRect partRect(int row, Part part) const;
	[[nodiscard]] std::pair<int, Part> partAt(QPoint position) const;
	void apply(std::vector<QString> ids);

	const not_null<Window::SessionController*> _controller;
	std::vector<QString> _ids;
	std::vector<Item> _items;
	std::pair<int, Part> _over = { -1, Part::None };

};

EditorList::EditorList(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller) {
	setMouseTracking(true);
	reload();
	Extera::Config::Instance().stringChanges(
		u"tristegram_dock_items"_q
	) | rpl::on_next([=](const QString &) {
		reload();
	}, lifetime());
}

void EditorList::reload() {
	const auto session = &_controller->session();
	_ids.clear();
	_items.clear();
	for (const auto &id : CurrentIds(session)) {
		if (auto item = ResolveItem(session, id)) {
			_ids.push_back(id);
			_items.push_back(std::move(*item));
		}
	}
	resizeToWidth(width());
	update();
}

void EditorList::apply(std::vector<QString> ids) {
	// An empty list would mean the default, keep one button at least.
	if (ids.empty()) {
		ids.push_back(u"b:saved"_q);
	}
	SaveIds(ids);
}

int EditorList::resizeGetHeight(int newWidth) {
	return std::max(int(_items.size()), 1) * Px(48) + Px(4);
}

QRect EditorList::partRect(int row, Part part) const {
	const auto size = Px(30);
	const auto top = row * Px(48) + (Px(48) - size) / 2;
	const auto right = width() - st::boxPadding.right();
	switch (part) {
	case Part::Remove: return QRect(right - size, top, size, size);
	case Part::Down: return QRect(right - 2 * size - Px(4), top, size, size);
	case Part::Up: return QRect(right - 3 * size - Px(8), top, size, size);
	case Part::None: break;
	}
	return QRect();
}

std::pair<int, EditorList::Part> EditorList::partAt(QPoint position) const {
	const auto row = position.y() / Px(48);
	if (row < 0 || row >= int(_items.size())) {
		return { -1, Part::None };
	}
	for (const auto part : { Part::Up, Part::Down, Part::Remove }) {
		if (partRect(row, part).contains(position)) {
			return { row, part };
		}
	}
	return { row, Part::None };
}

void EditorList::mouseMoveEvent(QMouseEvent *e) {
	const auto over = partAt(e->pos());
	if (_over != over) {
		_over = over;
		setCursor((over.second != Part::None)
			? style::cur_pointer
			: style::cur_default);
		update();
	}
}

void EditorList::leaveEventHook(QEvent *e) {
	_over = { -1, Part::None };
	update();
}

void EditorList::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	const auto [row, part] = partAt(e->pos());
	if (row < 0 || part == Part::None) {
		return;
	}
	auto ids = _ids;
	if (part == Part::Remove) {
		ids.erase(ids.begin() + row);
	} else if (part == Part::Up && row > 0) {
		std::swap(ids[row], ids[row - 1]);
	} else if (part == Part::Down && row + 1 < int(ids.size())) {
		std::swap(ids[row], ids[row + 1]);
	} else {
		return;
	}
	apply(std::move(ids));
}

void EditorList::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto left = st::boxPadding.left();
	for (auto i = 0; i != int(_items.size()); ++i) {
		const auto &item = _items[i];
		const auto top = i * Px(48);
		const auto icon = QRectF(left, top + Px(7), Px(34), Px(34));
		PaintItem(p, item, icon, colors);
		const auto textLeft = int(icon.right()) + Px(12);
		const auto textRight = partRect(i, Part::Up).x() - Px(8);
		p.setFont(st::normalFont);
		p.setPen(st::windowFg);
		p.drawText(
			QRect(textLeft, top, textRight - textLeft, Px(48)),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::normalFont->elided(item.title, textRight - textLeft));
		for (const auto part : { Part::Up, Part::Down, Part::Remove }) {
			const auto r = partRect(i, part);
			const auto over = (_over.first == i && _over.second == part);
			const auto disabled = (part == Part::Up && i == 0)
				|| (part == Part::Down && i + 1 == int(_items.size()));
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(
				(part == Part::Remove) ? st::attentionButtonFg->c : st::windowFg->c,
				over ? 0.16 : 0.07));
			p.drawRoundedRect(r, Px(9), Px(9));
			auto pen = QPen(
				Alpha(
					(part == Part::Remove)
						? st::attentionButtonFg->c
						: st::windowFg->c,
					disabled ? 0.25 : 0.8),
				Px(2),
				Qt::SolidLine,
				Qt::RoundCap,
				Qt::RoundJoin);
			p.setPen(pen);
			p.setBrush(Qt::NoBrush);
			const auto c = QRectF(r).center();
			const auto s = r.width() * 0.18;
			if (part == Part::Remove) {
				p.drawLine(c + QPointF(-s, -s), c + QPointF(s, s));
				p.drawLine(c + QPointF(-s, s), c + QPointF(s, -s));
			} else {
				const auto dir = (part == Part::Up) ? -1. : 1.;
				p.drawPolyline(QPolygonF({
					c + QPointF(-s * 1.2, -s * 0.6 * dir),
					c + QPointF(0, s * 0.6 * dir),
					c + QPointF(s * 1.2, -s * 0.6 * dir) }));
			}
		}
	}
}

void AddToDock(const QString &id, not_null<Main::Session*> session) {
	auto ids = CurrentIds(session);
	if (ranges::contains(ids, id)) {
		return;
	}
	ids.push_back(id);
	if (int(ids.size()) > kMaxItems) {
		ids.erase(ids.begin(), ids.end() - kMaxItems);
	}
	SaveIds(ids);
}

} // namespace

void InstallDock(
		not_null<QWidget*> parent,
		not_null<Window::SessionController*> controller) {
	Ui::CreateChild<Dock>(parent.get(), controller);
}

void ShowDockEditor(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgk_dock_items());
		box->setWidth(st::boxWideWidth);
		const auto session = &controller->session();
		box->addRow(
			object_ptr<EditorList>(box, controller),
			QMargins(0, 0, 0, st::boxPadding.bottom()));
		box->addButton(tr::lng_tgk_dock_add(), [=] {
			const auto menu = Ui::CreateChild<Ui::PopupMenu>(
				box.get(),
				st::popupMenuWithIcons);
			menu->setAttribute(Qt::WA_DeleteOnClose);
			menu->addAction(tr::lng_tgk_dock_add_chat(tr::now), [=] {
				Window::ShowChooseRecipientBox(
					controller,
					[=](not_null<Data::Thread*> thread) {
						AddToDock(
							u"p:"_q + QString::number(thread->peer()->id.value),
							session);
						return true;
					},
					tr::lng_tgk_dock_add_chat());
			}, &st::menuIconChats);
			for (const auto &filter : session->data().chatsFilters().list()) {
				const auto id = filter.id();
				const auto title = id
					? filter.titleText().text
					: tr::lng_filters_all(tr::now);
				menu->addAction(
					tr::lng_tgk_dock_add_folder(tr::now) + u": "_q + title,
					[=] { AddToDock(u"f:"_q + QString::number(id), session); },
					&st::menuIconAddToFolder);
			}
			for (const auto &button : ButtonIds()) {
				menu->addAction(
					ButtonTitle(button),
					[=] { AddToDock(u"b:"_q + button, session); },
					ButtonIcon(button) ? ButtonIcon(button) : &st::menuIconSavedMessages);
			}
			menu->popup(QCursor::pos());
		});
		box->addLeftButton(tr::lng_tgk_dock_reset(), [=] {
			SaveIds({});
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace Tristegram
