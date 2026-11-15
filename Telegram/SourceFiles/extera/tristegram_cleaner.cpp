/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_cleaner.h"

#include "apiwrap.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_row.h"
#include "extera/tristegram_custom_ui.h"
#include "extera/tristegram_logo.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QPointer>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>

namespace Tristegram {
namespace {

constexpr auto kDay = 86400;

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

enum class Kind {
	Channel,
	Group,
	Person,
	Bot,
};

[[nodiscard]] Kind KindOf(not_null<PeerData*> peer) {
	if (const auto channel = peer->asChannel()) {
		return channel->isBroadcast() ? Kind::Channel : Kind::Group;
	} else if (peer->isChat()) {
		return Kind::Group;
	} else if (const auto user = peer->asUser()) {
		return user->isBot() ? Kind::Bot : Kind::Person;
	}
	return Kind::Person;
}

[[nodiscard]] QString KindText(Kind kind) {
	switch (kind) {
	case Kind::Channel: return tr::lng_tgc_kind_channel(tr::now);
	case Kind::Group: return tr::lng_tgc_kind_group(tr::now);
	case Kind::Bot: return tr::lng_tgc_kind_bot(tr::now);
	case Kind::Person: return tr::lng_tgc_kind_person(tr::now);
	}
	return QString();
}

[[nodiscard]] QString Ago(TimeId when) {
	if (!when) {
		return tr::lng_tgc_never(tr::now);
	}
	const auto days = std::max((base::unixtime::now() - when) / kDay, 0);
	if (days >= 365) {
		return QString::number(days / 365) + ' ' + tr::lng_tgc_years(tr::now);
	} else if (days >= 30) {
		return QString::number(days / 30) + ' ' + tr::lng_tgc_months(tr::now);
	}
	return QString::number(days) + ' ' + tr::lng_tgc_days(tr::now);
}

struct Entry {
	not_null<History*> history;
	Kind kind = Kind::Person;
	TimeId last = 0;
	bool checked = true;
	Ui::PeerUserpicView userpic;
};

class CleanerList final : public Ui::RpWidget {
public:
	CleanerList(QWidget *parent);

	void setEntries(std::vector<Entry> entries);
	[[nodiscard]] std::vector<not_null<History*>> checked() const;
	[[nodiscard]] rpl::producer<int> checkedCount() const {
		return _checkedCount.value();
	}
	void toggleAll();

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void recount();

	std::vector<Entry> _entries;
	int _over = -1;
	rpl::variable<int> _checkedCount = 0;

};

CleanerList::CleanerList(QWidget *parent) : RpWidget(parent) {
	setMouseTracking(true);
}

void CleanerList::setEntries(std::vector<Entry> entries) {
	_entries = std::move(entries);
	_over = -1;
	recount();
	resizeToWidth(width());
	update();
}

void CleanerList::recount() {
	_checkedCount = int(ranges::count_if(_entries, &Entry::checked));
}

void CleanerList::toggleAll() {
	const auto all = ranges::all_of(_entries, &Entry::checked);
	for (auto &entry : _entries) {
		entry.checked = !all;
	}
	recount();
	update();
}

std::vector<not_null<History*>> CleanerList::checked() const {
	auto result = std::vector<not_null<History*>>();
	for (const auto &entry : _entries) {
		if (entry.checked) {
			result.push_back(entry.history);
		}
	}
	return result;
}

int CleanerList::resizeGetHeight(int newWidth) {
	return std::max(int(_entries.size()) * Px(58) + Px(8), Px(120));
}

void CleanerList::mouseMoveEvent(QMouseEvent *e) {
	const auto index = e->pos().y() / Px(58);
	const auto over = (index >= 0 && index < int(_entries.size())) ? index : -1;
	if (_over != over) {
		_over = over;
		setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void CleanerList::leaveEventHook(QEvent *e) {
	_over = -1;
	update();
}

void CleanerList::mousePressEvent(QMouseEvent *e) {
	const auto index = e->pos().y() / Px(58);
	if (e->button() == Qt::LeftButton
		&& index >= 0
		&& index < int(_entries.size())) {
		_entries[index].checked = !_entries[index].checked;
		recount();
		update();
	}
}

void CleanerList::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	if (_entries.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect().marginsRemoved({ Px(24), Px(24), Px(24), Px(24) }),
			Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
			tr::lng_tgc_empty(tr::now));
		return;
	}
	const auto colors = CurrentLogoStyle();
	const auto clip = e->rect();
	for (auto i = 0; i != int(_entries.size()); ++i) {
		const auto row = QRect(Px(8), i * Px(58), width() - Px(16), Px(58));
		if (!row.intersects(clip)) {
			continue;
		}
		auto &entry = _entries[i];
		if (i == _over) {
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(st::windowFg->c, 0.05));
			p.drawRoundedRect(QRectF(row).marginsRemoved({ 0, 2, 0, 2 }), Px(12), Px(12));
		}
		// The check, a gradient circle when chosen.
		const auto check = QRectF(
			row.x() + Px(10),
			row.y() + (row.height() - Px(22)) / 2.,
			Px(22),
			Px(22));
		if (entry.checked) {
			auto gradient = QLinearGradient(check.topLeft(), check.bottomRight());
			gradient.setColorAt(0., colors.from);
			gradient.setColorAt(1., colors.to);
			p.setPen(Qt::NoPen);
			p.setBrush(gradient);
			p.drawEllipse(check);
			p.setPen(QPen(QColor(255, 255, 255), Px(2), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			const auto c = check.center();
			const auto s = check.width() * 0.2;
			auto path = QPainterPath();
			path.moveTo(c.x() - s * 1.2, c.y());
			path.lineTo(c.x() - s * 0.3, c.y() + s);
			path.lineTo(c.x() + s * 1.3, c.y() - s * 0.9);
			p.setBrush(Qt::NoBrush);
			p.drawPath(path);
		} else {
			p.setPen(QPen(Alpha(st::windowFg->c, 0.3), Px(2)));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(check.marginsRemoved({ 1, 1, 1, 1 }));
		}
		const auto photo = Px(40);
		const auto photoLeft = int(check.right()) + Px(12);
		const auto peer = entry.history->peer;
		peer->paintUserpic(
			p,
			entry.userpic,
			photoLeft,
			row.y() + (row.height() - photo) / 2,
			photo);
		const auto left = photoLeft + photo + Px(12);
		const auto available = row.right() - left - Px(10);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowBoldFg);
		p.drawText(
			QRect(left, row.y() + Px(10), available, st::semiboldFont->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(peer->name(), available));
		auto meta = KindText(entry.kind);
		if (const auto channel = peer->asChannel()) {
			if (channel->membersCount() > 0) {
				meta += u" · "_q + QString::number(channel->membersCount());
			}
		}
		meta += u" · "_q + tr::lng_tgc_quiet_for(tr::now) + ' ' + Ago(entry.last);
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(left, row.y() + Px(12) + st::semiboldFont->height, available, st::normalFont->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::normalFont->elided(meta, available));
	}
}

class CleanerContent final : public Ui::RpWidget {
public:
	CleanerContent(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> close,
		int height);

protected:
	int resizeGetHeight(int newWidth) override {
		return _height;
	}
	void paintEvent(QPaintEvent *e) override;

private:
	void refill();
	void archive();
	void leave();
	[[nodiscard]] std::vector<Entry> collect() const;

	const not_null<Window::SessionController*> _controller;
	const Fn<void()> _close;
	const int _height = 0;
	int _period = 1;
	int _kinds = 0x0F;
	not_null<Ui::RpWidget*> _periodTabs;
	not_null<Ui::RpWidget*> _kindTabs;
	const not_null<Ui::ScrollArea*> _scroll;
	const not_null<CleanerList*> _list;
	const not_null<Ui::RoundButton*> _select;
	const not_null<Ui::RoundButton*> _archive;
	const not_null<Ui::RoundButton*> _leaveButton;
	const not_null<Ui::IconButton*> _closeButton;

};

[[nodiscard]] TimeId PeriodSeconds(int index) {
	switch (index) {
	case 0: return 30 * kDay;
	case 1: return 90 * kDay;
	case 2: return 180 * kDay;
	}
	return 365 * kDay;
}

CleanerContent::CleanerContent(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Fn<void()> close,
	int height)
: RpWidget(parent)
, _controller(controller)
, _close(std::move(close))
, _height(height)
, _periodTabs(CreateSegmentedTabs(
	this,
	{
		tr::lng_tgc_period_month(tr::now),
		tr::lng_tgc_period_3(tr::now),
		tr::lng_tgc_period_6(tr::now),
		tr::lng_tgc_period_year(tr::now),
	},
	1,
	[=](int index) { _period = index; refill(); }).release())
, _kindTabs(CreateSegmentedTabs(
	this,
	{
		tr::lng_tgc_all(tr::now),
		tr::lng_tgc_channels(tr::now),
		tr::lng_tgc_groups(tr::now),
		tr::lng_tgc_people(tr::now),
		tr::lng_tgc_bots(tr::now),
	},
	0,
	[=](int index) {
		_kinds = (index == 0) ? 0x0F : (1 << (index - 1));
		refill();
	}).release())
, _scroll(Ui::CreateChild<Ui::ScrollArea>(this, st::boxScroll))
, _list(_scroll->setOwnedWidget(object_ptr<CleanerList>(_scroll.get())))
, _select(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_tgc_select_all(),
	st::defaultLightButton))
, _archive(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_tgc_archive(),
	st::defaultLightButton))
, _leaveButton(Ui::CreateChild<Ui::RoundButton>(
	this,
	tr::lng_tgc_leave(),
	st::defaultActiveButton))
, _closeButton(Ui::CreateChild<Ui::IconButton>(this, st::boxTitleClose)) {
	_closeButton->setClickedCallback([=] { _close(); });
	_select->setClickedCallback([=] { _list->toggleAll(); });
	_archive->setClickedCallback([=] { archive(); });
	_leaveButton->setClickedCallback([=] { leave(); });
	_list->checkedCount(
	) | rpl::on_next([=](int count) {
		_archive->setText(rpl::single(
			tr::lng_tgc_archive(tr::now) + u" ("_q + QString::number(count) + ')'));
		_leaveButton->setText(rpl::single(
			tr::lng_tgc_leave(tr::now) + u" ("_q + QString::number(count) + ')'));
		_archive->setDisabled(!count);
		_leaveButton->setDisabled(!count);
	}, lifetime());

	sizeValue(
	) | rpl::on_next([=](QSize size) {
		const auto side = Px(16);
		_closeButton->moveToRight(Px(10), Px(10));
		_periodTabs->resizeToWidth(size.width() - 2 * side);
		_periodTabs->moveToLeft(side, Px(92));
		_kindTabs->resizeToWidth(size.width() - 2 * side);
		_kindTabs->moveToLeft(side, Px(92) + _periodTabs->height() + Px(8));
		const auto top = _kindTabs->y() + _kindTabs->height() + Px(10);
		const auto footer = Px(64);
		_scroll->setGeometry(0, top, size.width(), size.height() - top - footer);
		_list->resizeToWidth(size.width());
		const auto buttonsTop = size.height() - footer + (footer - _leaveButton->height()) / 2;
		_leaveButton->moveToRight(side, buttonsTop);
		_archive->moveToRight(side + _leaveButton->width() + Px(8), buttonsTop);
		_select->moveToLeft(side, buttonsTop);
	}, lifetime());

	refill();
}

std::vector<Entry> CleanerContent::collect() const {
	auto result = std::vector<Entry>();
	const auto session = &_controller->session();
	const auto border = base::unixtime::now() - PeriodSeconds(_period);
	for (const auto &row : session->data().chatsList()->indexed()->all()) {
		const auto history = row->history();
		if (!history) {
			continue;
		}
		const auto peer = history->peer;
		if (peer->isSelf()
			|| peer->isRepliesChat()
			|| peer->isVerifyCodes()
			|| peer->isServiceUser()
			|| history->isPinnedDialog(0)) {
			continue;
		}
		const auto last = history->chatListTimeId();
		if (last > border) {
			continue;
		}
		const auto kind = KindOf(peer);
		if (!(_kinds & (1 << int(kind)))) {
			continue;
		}
		result.push_back(Entry{ .history = history, .kind = kind, .last = last });
	}
	ranges::sort(result, ranges::less(), &Entry::last);
	return result;
}

void CleanerContent::refill() {
	_list->setEntries(collect());
	update();
}

void CleanerContent::archive() {
	const auto list = _list->checked();
	if (list.empty()) {
		return;
	}
	const auto session = &_controller->session();
	for (const auto history : list) {
		session->api().toggleHistoryArchived(history, true, nullptr);
	}
	_controller->showToast(tr::lng_tgc_archived(tr::now)
		+ ' '
		+ QString::number(list.size()));
	refill();
}

void CleanerContent::leave() {
	const auto list = _list->checked();
	if (list.empty()) {
		return;
	}
	const auto controller = _controller;
	const auto weak = QPointer<CleanerContent>(this);
	const auto count = int(list.size());
	controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_tgc_leave_sure(tr::now)
			+ ' '
			+ QString::number(count)
			+ '?',
		.confirmed = [=](Fn<void()> close) {
			const auto session = &controller->session();
			for (const auto history : list) {
				const auto peer = history->peer;
				if (const auto channel = peer->asChannel()) {
					session->api().leaveChannel(channel);
				} else {
					session->api().deleteConversation(peer, false);
				}
			}
			close();
			if (weak) {
				weak->refill();
			}
			controller->showToast(tr::lng_tgc_left(tr::now)
				+ ' '
				+ QString::number(count));
		},
		.confirmText = tr::lng_tgc_leave(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void CleanerContent::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto tile = QRectF(Px(18), Px(16), Px(40), Px(40));
	auto gradient = QLinearGradient(tile.topLeft(), tile.bottomRight());
	gradient.setColorAt(0., colors.from);
	gradient.setColorAt(1., colors.to);
	p.setPen(Qt::NoPen);
	p.setBrush(gradient);
	p.drawRoundedRect(tile, Px(12), Px(12));
	// A broom: a handle and bristles.
	p.setPen(QPen(QColor(255, 255, 255), Px(2), Qt::SolidLine, Qt::RoundCap));
	const auto c = tile.center();
	const auto s = tile.width() * 0.28;
	p.drawLine(QPointF(c.x() + s, c.y() - s), QPointF(c.x() - s * 0.2, c.y() + s * 0.2));
	auto bristles = QPainterPath();
	bristles.moveTo(c.x() - s * 0.05, c.y() + s * 0.05);
	bristles.lineTo(c.x() - s * 1.0, c.y() + s * 0.25);
	bristles.lineTo(c.x() - s * 0.6, c.y() + s * 1.1);
	bristles.closeSubpath();
	p.setBrush(QColor(255, 255, 255));
	p.setPen(Qt::NoPen);
	p.drawPath(bristles);

	const auto left = int(tile.right()) + Px(12);
	p.setFont(st::boxTitle.style.font);
	p.setPen(st::boxTitleFg);
	p.drawText(
		QRect(left, Px(14), width() - left - Px(50), Px(24)),
		Qt::AlignLeft | Qt::AlignVCenter,
		tr::lng_tgc_title(tr::now));
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(left, Px(38), width() - left - Px(20), Px(40)),
		Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
		tr::lng_tgc_about(tr::now));
	p.fillRect(
		QRect(0, height() - Px(64), width(), st::lineWidth),
		Alpha(st::windowFg->c, 0.08));
}

} // namespace

void ShowChatCleaner(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto window = controller->widget();
		const auto width = std::min(Px(560), window->width() - Px(40));
		const auto height = std::min(Px(640), window->height() - Px(60));
		box->setNoContentMargin(true);
		box->setWidth(width);
		box->addRow(
			object_ptr<CleanerContent>(
				box,
				controller,
				[=] { box->closeBox(); },
				height),
			QMargins());
	}));
}

} // namespace Tristegram
