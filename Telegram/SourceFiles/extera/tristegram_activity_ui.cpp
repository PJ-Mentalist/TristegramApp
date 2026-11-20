/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_activity_ui.h"

#include "base/timer.h"
#include "extera/extera_config.h"
#include "extera/tristegram_activity.h"
#include "extera/tristegram_custom.h"
#include "extera/tristegram_custom_ui.h"
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"

#include <QtCore/QDate>
#include <QtCore/QPointer>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainterPath>

#include <algorithm>

namespace Tristegram {
namespace {

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(alpha);
	return color;
}

[[nodiscard]] QString FormatDuration(int64 seconds) {
	const auto minutes = seconds / 60;
	const auto hours = minutes / 60;
	if (hours > 0) {
		return tr::lng_tgac_hours(
			tr::now,
			lt_hours,
			QString::number(hours),
			lt_minutes,
			QString::number(minutes % 60));
	}
	return tr::lng_tgac_minutes(
		tr::now,
		lt_minutes,
		QString::number(std::max(minutes, int64(seconds > 0 ? 1 : 0))));
}

[[nodiscard]] QString KindTitle(const QString &kind) {
	if (kind == u"game"_q) {
		return tr::lng_tgac_kind_game(tr::now);
	} else if (kind == u"music"_q) {
		return tr::lng_tgac_kind_music(tr::now);
	} else if (kind == u"coding"_q) {
		return tr::lng_tgac_kind_coding(tr::now);
	} else if (kind == u"video"_q) {
		return tr::lng_tgac_kind_video(tr::now);
	}
	return QString();
}

// The chart of the last two weeks and the list for the chosen period.
class ActivityView final : public Ui::RpWidget {
public:
	ActivityView(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setPeriod(Activity::Period period);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	static constexpr auto kDays = 14;

	void refresh();
	[[nodiscard]] int listTop() const;
	[[nodiscard]] int rowAt(QPoint position) const;
	void paintNow(QPainter &p, QRect rect, crl::time now);
	void paintChart(QPainter &p, QRect rect);

	const not_null<Window::SessionController*> _controller;
	Activity::Period _period = Activity::Period::Week;
	std::vector<Activity::StatEntry> _entries;
	std::vector<int64> _daily;
	base::Timer _tick;
	int _over = -1;

};

ActivityView::ActivityView(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _tick([=] { update(0, 0, width(), Px(86)); }) {
	setMouseTracking(true);
	refresh();
	_tick.callEach(250);
	Activity::OwnChanges(
	) | rpl::on_next([=] {
		refresh();
	}, lifetime());
}

void ActivityView::setPeriod(Activity::Period period) {
	_period = period;
	refresh();
}

void ActivityView::refresh() {
	_entries = Activity::Stats(_period);
	_daily = Activity::DailyTotals(kDays);
	resizeToWidth(width());
	update();
}

int ActivityView::listTop() const {
	return Px(86) + Px(170);
}

int ActivityView::resizeGetHeight(int newWidth) {
	const auto rows = std::max(int(_entries.size()), 1);
	return listTop() + rows * Px(52) + Px(12);
}

int ActivityView::rowAt(QPoint position) const {
	const auto top = listTop();
	if (position.y() < top) {
		return -1;
	}
	const auto index = (position.y() - top) / Px(52);
	return (index < int(_entries.size())) ? index : -1;
}

void ActivityView::mouseMoveEvent(QMouseEvent *e) {
	const auto over = Activity::IsOwner() ? rowAt(e->pos()) : -1;
	if (_over != over) {
		_over = over;
		setCursor((over >= 0) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void ActivityView::leaveEventHook(QEvent *e) {
	if (_over >= 0) {
		_over = -1;
		update();
	}
}

void ActivityView::mousePressEvent(QMouseEvent *e) {
	if (!Activity::IsOwner()) {
		return;
	}
	const auto index = rowAt(e->pos());
	if (index < 0) {
		return;
	}
	// Only the owner sees this: the total hours can be written by hand.
	const auto entry = _entries[index];
	const auto all = Activity::Stats(Activity::Period::All);
	const auto i = ranges::find(all, entry.key, &Activity::StatEntry::key);
	const auto current = (i != end(all)) ? (i->seconds / 3600.) : 0.;
	AskText(
		_controller,
		entry.name,
		tr::lng_tgac_owner_hours(tr::now),
		QString::number(current, 'f', 1),
		crl::guard(this, [=](QString value) {
			auto ok = false;
			const auto hours = value.replace(',', '.').toDouble(&ok);
			if (ok) {
				Activity::SetTotalHours(entry.key, hours);
				refresh();
			}
		}));
}

void ActivityView::paintNow(QPainter &p, QRect rect, crl::time now) {
	const auto colors = CurrentLogoStyle();
	auto hq = PainterHighQualityEnabler(p);
	const auto card = QRectF(rect).marginsRemoved({ 0., 4., 0., 6. });
	auto wash = QLinearGradient(card.topLeft(), card.bottomRight());
	wash.setColorAt(0., Alpha(colors.from, 0.22));
	wash.setColorAt(1., Alpha(colors.to, 0.08));
	p.setPen(Qt::NoPen);
	p.setBrush(wash);
	p.drawRoundedRect(card, Px(18), Px(18));
	const auto own = Activity::Own();
	const auto icon = QRectF(
		card.x() + Px(18),
		card.center().y() - Px(14),
		Px(28),
		Px(28));
	if (own) {
		Activity::PaintIcon(p, icon, own->status.kind, now);
	}
	const auto left = int(icon.right()) + Px(14);
	const auto available = int(card.right()) - left - Px(16);
	p.setFont(st::semiboldFont);
	p.setPen(st::windowBoldFg);
	const auto title = own
		? Activity::Describe(own->status)
		: tr::lng_tgac_now_nothing(tr::now);
	p.drawText(
		QRect(left, int(card.y()) + Px(16), available, st::semiboldFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::semiboldFont->elided(title, available));
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	const auto sub = own
		? tr::lng_tgac_now_for(
			tr::now,
			lt_time,
			FormatDuration((crl::now() - own->since) / 1000))
		: tr::lng_tgac_now_hint(tr::now);
	p.drawText(
		QRect(
			left,
			int(card.y()) + Px(20) + st::semiboldFont->height,
			available,
			st::normalFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		st::normalFont->elided(sub, available));
}

void ActivityView::paintChart(QPainter &p, QRect rect) {
	const auto colors = CurrentLogoStyle();
	auto hq = PainterHighQualityEnabler(p);
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		QRect(rect.x(), rect.y(), rect.width(), Px(24)),
		Qt::AlignLeft | Qt::AlignVCenter,
		tr::lng_tgac_chart(tr::now));
	const auto area = QRectF(
		rect.x(),
		rect.y() + Px(30),
		rect.width(),
		rect.height() - Px(52));
	const auto max = std::max(
		*std::max_element(_daily.begin(), _daily.end()),
		int64(600));
	const auto count = int(_daily.size());
	const auto slot = area.width() / count;
	const auto bar = slot * 0.62;
	const auto today = QDate::currentDate();
	for (auto i = 0; i != count; ++i) {
		const auto value = _daily[i] / double(max);
		const auto h = std::max(area.height() * value, double(Px(3)));
		const auto x = area.x() + slot * i + (slot - bar) / 2.;
		const auto r = QRectF(x, area.bottom() - h, bar, h);
		auto fill = QLinearGradient(r.bottomLeft(), r.topLeft());
		fill.setColorAt(0., colors.from);
		fill.setColorAt(1., colors.to);
		p.setPen(Qt::NoPen);
		p.setBrush(_daily[i] ? QBrush(fill) : QBrush(Alpha(st::windowFg->c, 0.1)));
		p.drawRoundedRect(r, bar / 3., bar / 3.);
		if (i % 2 == (count - 1) % 2) {
			const auto day = today.addDays(-(count - 1 - i));
			p.setPen(st::windowSubTextFg);
			p.drawText(
				QRectF(area.x() + slot * i - Px(10), area.bottom() + Px(4), slot + Px(20), Px(16)),
				Qt::AlignHCenter | Qt::AlignTop,
				QString::number(day.day()));
		}
	}
}

void ActivityView::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto now = crl::now();
	paintNow(p, QRect(0, 0, width(), Px(86)), now);
	paintChart(p, QRect(0, Px(86), width(), Px(170)));

	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto top = listTop();
	if (_entries.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(0, top, width(), Px(52)),
			Qt::AlignCenter | Qt::TextWordWrap,
			tr::lng_tgac_empty(tr::now));
		return;
	}
	const auto max = std::max(_entries.front().seconds, int64(1));
	for (auto i = 0; i != int(_entries.size()); ++i) {
		const auto &entry = _entries[i];
		const auto row = QRect(0, top + i * Px(52), width(), Px(52));
		if (i == _over) {
			p.setPen(Qt::NoPen);
			p.setBrush(Alpha(st::windowFg->c, 0.05));
			p.drawRoundedRect(QRectF(row).marginsRemoved({ 0., 2., 0., 2. }), Px(12), Px(12));
		}
		const auto icon = QRectF(row.x() + Px(10), row.y() + Px(13), Px(26), Px(26));
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(colors.from, 0.15));
		p.drawRoundedRect(icon.marginsAdded({ 3., 3., 3., 3. }), Px(9), Px(9));
		Activity::PaintIcon(p, icon.marginsRemoved({ 3., 3., 3., 3. }), entry.kind, 0);
		const auto left = int(icon.right()) + Px(14);
		const auto timeText = FormatDuration(entry.seconds);
		const auto timeWidth = st::semiboldFont->width(timeText) + Px(8);
		const auto available = width() - left - timeWidth - Px(10);
		p.setFont(st::semiboldFont);
		p.setPen(st::windowBoldFg);
		p.drawText(
			QRect(left, row.y() + Px(8), available, st::semiboldFont->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(entry.name, available));
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(left, row.y() + Px(10) + st::semiboldFont->height, available, st::normalFont->height),
			Qt::AlignLeft | Qt::AlignVCenter,
			KindTitle(entry.kind));
		p.setFont(st::semiboldFont);
		p.setPen(st::windowBoldFg);
		p.drawText(
			QRect(width() - timeWidth - Px(10), row.y() + Px(8), timeWidth, st::semiboldFont->height),
			Qt::AlignRight | Qt::AlignVCenter,
			timeText);
		const auto bar = QRectF(
			width() - Px(10) - Px(70),
			row.y() + Px(34),
			Px(70),
			Px(4));
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowFg->c, 0.1));
		p.drawRoundedRect(bar, Px(2), Px(2));
		auto fill = QLinearGradient(bar.topLeft(), bar.topRight());
		fill.setColorAt(0., colors.from);
		fill.setColorAt(1., colors.to);
		p.setBrush(fill);
		p.drawRoundedRect(
			QRectF(bar.x(), bar.y(), bar.width() * entry.seconds / double(max), bar.height()),
			Px(2),
			Px(2));
	}
}

// The capsule at the bottom of the chat list.
class ActivityChip final : public Ui::RpWidget {
public:
	ActivityChip(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: RpWidget(parent)
	, _controller(controller)
	, _tick([=] { refresh(); }) {
		setCursor(style::cur_pointer);
		_tick.callEach(100);
		hide();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		const auto own = Activity::Own();
		if (!own) {
			return;
		}
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto colors = CurrentLogoStyle();
		const auto r = QRectF(rect()).marginsRemoved({ 1., 1., 1., 1. });
		const auto radius = r.height() / 2.;
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(r, radius, radius);
		auto wash = QLinearGradient(r.topLeft(), r.topRight());
		wash.setColorAt(0., Alpha(colors.from, 0.3));
		wash.setColorAt(1., Alpha(colors.to, 0.12));
		p.setBrush(wash);
		p.drawRoundedRect(r, radius, radius);
		auto rim = QLinearGradient(r.topLeft(), r.topRight());
		rim.setColorAt(0., Alpha(colors.from, 0.7));
		rim.setColorAt(1., Alpha(colors.to, 0.4));
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QBrush(rim), 1.2));
		p.drawRoundedRect(r, radius, radius);

		const auto icon = QRectF(Px(12), (height() - Px(20)) / 2., Px(20), Px(20));
		Activity::PaintIcon(p, icon, own->status.kind, crl::now());
		const auto timeText = FormatClock((crl::now() - own->since) / 1000);
		const auto timeWidth = st::semiboldFont->width(timeText) + Px(14);
		const auto left = int(icon.right()) + Px(10);
		const auto available = width() - left - timeWidth;
		p.setFont(st::semiboldFont);
		p.setPen(st::windowBoldFg);
		p.drawText(
			QRect(left, 0, available, height()),
			Qt::AlignLeft | Qt::AlignVCenter,
			st::semiboldFont->elided(Activity::Describe(own->status), available));
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(width() - timeWidth, 0, timeWidth - Px(12), height()),
			Qt::AlignRight | Qt::AlignVCenter,
			timeText);
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
			ShowActivityWindow(_controller);
		}
	}

private:
	[[nodiscard]] static QString FormatClock(int64 seconds) {
		const auto h = seconds / 3600;
		const auto m = (seconds / 60) % 60;
		const auto s = seconds % 60;
		return (h ? (QString::number(h) + ':') : QString())
			+ QString::number(m).rightJustified(h ? 2 : 1, '0')
			+ ':'
			+ QString::number(s).rightJustified(2, '0');
	}
	void refresh() {
		const auto allowed = property("tristegramAllowed").toBool()
			&& Extera::Bool(u"tristegram_activity_chip"_q);
		const auto shown = allowed && Activity::Own().has_value();
		if (isHidden() == shown) {
			setVisible(shown);
			if (shown) {
				raise();
			}
		}
		if (shown) {
			update();
		}
	}

	const not_null<Window::SessionController*> _controller;
	base::Timer _tick;

};

} // namespace

void ShowActivityWindow(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgac_title());
		box->setWidth(st::boxWideWidth);
		box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
		const auto content = box->verticalLayout();
		const auto view = std::make_shared<QPointer<ActivityView>>();
		content->add(
			CreateSegmentedTabs(
				content,
				{
					tr::lng_tgac_today(tr::now),
					tr::lng_tgac_week(tr::now),
					tr::lng_tgac_all(tr::now),
				},
				1,
				[=](int index) {
					if (const auto strong = view->data()) {
						strong->setPeriod((index == 0)
							? Activity::Period::Today
							: (index == 2)
							? Activity::Period::All
							: Activity::Period::Week);
					}
				}),
			QMargins(0, 0, 0, Px(6)));
		*view = content->add(
			object_ptr<ActivityView>(content, controller),
			QMargins(Px(16), 0, Px(16), Px(8)));
		box->addButton(tr::lng_tgac_add_game(), [=] {
			const auto windows = Activity::RunningWindows();
			if (windows.empty()) {
				controller->showToast(tr::lng_tga_games_none(tr::now));
				return;
			}
			auto options = std::vector<QString>();
			for (const auto &window : windows) {
				options.push_back(window.title + u"  ("_q + window.exe + ')');
			}
			controller->show(Box([=](not_null<Ui::GenericBox*> choose) {
				SingleChoiceBox(choose, {
					.title = tr::lng_tga_games_running(),
					.options = options,
					.initialSelection = -1,
					.callback = [=](int index) {
						if (index < 0 || index >= int(windows.size())) {
							return;
						}
						const auto window = windows[index];
						crl::on_main(controller, [=] {
							AskText(
								controller,
								tr::lng_tga_games_name(tr::now),
								tr::lng_tga_games_name(tr::now),
								window.title,
								[=](QString name) {
									Activity::AddCustomGame(window.exe, name);
								});
						});
					},
				});
			}));
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

object_ptr<Ui::RpWidget> CreateActivityChip(
		QWidget *parent,
		not_null<Window::SessionController*> controller) {
	return object_ptr<ActivityChip>(parent, controller);
}

} // namespace Tristegram
