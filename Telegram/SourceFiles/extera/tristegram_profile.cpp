/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_profile.h"

#include "base/call_delayed.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"

#include <QtCore/QPointer>
#include <QtGui/QPainterPath>

namespace Tristegram {
namespace {

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

void OpenOwnProfile(not_null<Main::Account*> account, int attempt = 0) {
	const auto window = Core::App().activePrimaryWindow();
	const auto controller = window ? window->sessionController() : nullptr;
	if (controller
		&& account->sessionExists()
		&& (&controller->session() == &account->session())) {
		controller->showPeerInfo(controller->session().user());
	} else if (attempt < 10) {
		base::call_delayed(150, [=] {
			OpenOwnProfile(account, attempt + 1);
		});
	}
}

class AccountArrow final : public Ui::AbstractButton {
public:
	AccountArrow(QWidget *parent, bool left);

	void setTarget(Main::Account *account);
	[[nodiscard]] Main::Account *target() const {
		return _target;
	}

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	const bool _left = false;
	Main::Account *_target = nullptr;
	Ui::PeerUserpicView _userpic;
	QString _name;

};

AccountArrow::AccountArrow(QWidget *parent, bool left)
: AbstractButton(parent)
, _left(left) {
	resize(Px(110), Px(124));
	setPointerCursor(true);
}

void AccountArrow::setTarget(Main::Account *account) {
	_target = account;
	_userpic = {};
	_name = (account && account->sessionExists())
		? account->session().user()->shortName()
		: QString();
	update();
}

void AccountArrow::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto over = isOver() || isDown();
	const auto colors = CurrentLogoStyle();

	// A glass circle with a chevron.
	const auto size = Px(over ? 56 : 52);
	const auto circle = QRectF(
		(width() - size) / 2.,
		Px(4) + (Px(56) - size) / 2.,
		size,
		size);
	if (over) {
		auto glow = QRadialGradient(circle.center(), size * 0.9);
		glow.setColorAt(0., Alpha(colors.from, 0.45));
		glow.setColorAt(1., Alpha(colors.to, 0.));
		p.setPen(Qt::NoPen);
		p.setBrush(glow);
		p.drawEllipse(circle.center(), size * 0.9, size * 0.9);
	}
	p.setPen(QPen(QColor(255, 255, 255, over ? 90 : 50), 1.));
	if (over) {
		auto gradient = QLinearGradient(circle.topLeft(), circle.bottomRight());
		gradient.setColorAt(0., colors.from);
		gradient.setColorAt(1., colors.to);
		p.setBrush(gradient);
	} else {
		p.setBrush(QColor(255, 255, 255, 34));
	}
	p.drawEllipse(circle);
	auto sheen = QLinearGradient(circle.topLeft(), circle.bottomLeft());
	sheen.setColorAt(0., QColor(255, 255, 255, 40));
	sheen.setColorAt(0.5, QColor(255, 255, 255, 0));
	p.setPen(Qt::NoPen);
	p.setBrush(sheen);
	p.drawEllipse(circle);

	const auto c = circle.center();
	const auto s = size * 0.16;
	const auto dir = _left ? -1. : 1.;
	p.setPen(QPen(QColor(255, 255, 255), Px(3), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	auto chevron = QPainterPath();
	chevron.moveTo(c.x() - s * 0.6 * dir, c.y() - s * 1.2);
	chevron.lineTo(c.x() + s * 0.6 * dir, c.y());
	chevron.lineTo(c.x() - s * 0.6 * dir, c.y() + s * 1.2);
	p.drawPath(chevron);

	// The account it leads to.
	if (_target && _target->sessionExists()) {
		const auto user = _target->session().user();
		const auto photo = Px(30);
		const auto x = (width() - photo) / 2;
		const auto y = Px(66);
		user->paintUserpic(p, _userpic, x, y, photo);
		auto ring = QLinearGradient(
			QPointF(x, y),
			QPointF(x + photo, y + photo));
		ring.setColorAt(0., colors.from);
		ring.setColorAt(1., colors.to);
		p.setPen(QPen(QBrush(ring), Px(2)));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(QRectF(x, y, photo, photo).marginsAdded({ 2, 2, 2, 2 }));
		p.setPen(QColor(255, 255, 255, over ? 255 : 200));
		p.setFont(st::normalFont->f);
		p.drawText(
			QRect(0, y + photo + Px(6), width(), st::normalFont->height),
			Qt::AlignHCenter | Qt::AlignVCenter,
			st::normalFont->elided(_name, width() - Px(4)));
	}
}

} // namespace

void SetupAccountArrows(
		not_null<Ui::RpWidget*> layer,
		not_null<PeerData*> peer) {
	if (!peer->isSelf()
		|| !Extera::Bool(u"tristegram_profile_arrows"_q)) {
		return;
	}
	const auto parent = layer->parentWidget();
	if (!parent) {
		return;
	}
	const auto left = Ui::CreateChild<AccountArrow>(parent, true);
	const auto right = Ui::CreateChild<AccountArrow>(parent, false);
	layer->lifetime().add([
			left = QPointer<AccountArrow>(left),
			right = QPointer<AccountArrow>(right)] {
		delete left.data();
		delete right.data();
	});

	const auto refresh = [=] {
		const auto accounts = Core::App().domain().orderedAccounts();
		auto ready = std::vector<not_null<Main::Account*>>();
		for (const auto account : accounts) {
			if (account->sessionExists()) {
				ready.push_back(account);
			}
		}
		const auto count = int(ready.size());
		const auto mine = &peer->session().account();
		const auto current = ranges::find_if(
			ready,
			[&](not_null<Main::Account*> account) {
				return account.get() == mine;
			});
		if (count < 2 || current == ready.end()) {
			left->hide();
			right->hide();
			return;
		}
		const auto index = int(current - ready.begin());
		left->setTarget(ready[(index + count - 1) % count]);
		right->setTarget(ready[(index + 1) % count]);
		left->setVisible(layer->isVisible());
		right->setVisible(layer->isVisible());
		left->raise();
		right->raise();
	};
	const auto place = [=] {
		const auto geometry = layer->geometry();
		const auto skip = Px(24);
		const auto top = geometry.y()
			+ std::min(geometry.height(), Px(400)) / 2
			- left->height() / 2;
		left->move(geometry.x() - skip - left->width(), top);
		right->move(geometry.x() + geometry.width() + skip, top);
		if (left->x() < 0 || right->x() + right->width() > parent->width()) {
			left->hide();
			right->hide();
		}
	};
	const auto clicked = [=](not_null<AccountArrow*> arrow) {
		const auto account = arrow->target();
		if (!account) {
			return;
		}
		const auto target = not_null<Main::Account*>(account);
		// Switching closes this layer with the arrows, so do it later.
		crl::on_main([=] {
			Core::App().domain().maybeActivate(target);
			OpenOwnProfile(target);
		});
	};
	left->setClickedCallback([=] { clicked(left); });
	right->setClickedCallback([=] { clicked(right); });

	refresh();
	place();
	layer->geometryValue(
	) | rpl::on_next([=] {
		refresh();
		place();
	}, left->lifetime());
	layer->shownValue(
	) | rpl::on_next([=] {
		refresh();
		place();
	}, left->lifetime());
	Core::App().domain().accountsChanges(
	) | rpl::on_next([=] {
		refresh();
		place();
	}, left->lifetime());
}

void PaintProfileGlow(QPainter &p, QRect area, QRect userpic) {
	if (!Extera::Bool(u"tristegram_profile_glow"_q)) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto center = QPointF(userpic.center());
	const auto radius = std::max(userpic.width() * 1.9, area.width() * 0.55);
	auto glow = QRadialGradient(center, radius);
	glow.setColorAt(0., Alpha(colors.from, 0.42));
	glow.setColorAt(0.45, Alpha(colors.to, 0.16));
	glow.setColorAt(1., Alpha(colors.to, 0.));
	p.setPen(Qt::NoPen);
	p.setBrush(glow);
	p.drawRect(area);
}

void PaintProfileRing(QPainter &p, QRect userpic) {
	if (!Extera::Bool(u"tristegram_profile_glow"_q)) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto colors = CurrentLogoStyle();
	const auto width = Px(3);
	const auto skip = width / 2. + Px(3);
	const auto frame = QRectF(userpic).marginsAdded({ skip, skip, skip, skip });
	auto gradient = QConicalGradient(frame.center(), 90.);
	gradient.setColorAt(0., colors.from);
	gradient.setColorAt(0.5, colors.to);
	gradient.setColorAt(1., colors.from);
	p.setPen(QPen(QBrush(gradient), width));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(frame);
}

} // namespace Tristegram
