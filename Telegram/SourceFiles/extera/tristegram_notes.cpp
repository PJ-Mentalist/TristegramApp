/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_notes.h"

#include "data/data_peer.h"
#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Tristegram {
namespace {

constexpr auto kMaxLength = 300;

[[nodiscard]] QString Key() {
	return u"tristegram_notes"_q;
}

[[nodiscard]] QJsonObject All() {
	return QJsonDocument::fromJson(
		Extera::String(Key()).toUtf8()).object();
}

[[nodiscard]] QString PeerKey(not_null<PeerData*> peer) {
	return QString::number(peer->id.value);
}

} // namespace

QString ChatNote(not_null<PeerData*> peer) {
	return All().value(PeerKey(peer)).toString();
}

void SetChatNote(not_null<PeerData*> peer, const QString &text) {
	auto all = All();
	const auto trimmed = text.trimmed().left(kMaxLength);
	if (trimmed.isEmpty()) {
		all.remove(PeerKey(peer));
	} else {
		all.insert(PeerKey(peer), trimmed);
	}
	Extera::Config::Instance().setString(
		Key(),
		QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
}

rpl::producer<> ChatNotesChanged() {
	return Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key == Key();
	}) | rpl::to_empty;
}

void EditChatNote(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgn_title());
		const auto current = ChatNote(peer);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			tr::lng_tgn_placeholder(),
			current));
		field->setMaxLength(kMaxLength);
		const auto save = [=] {
			SetChatNote(peer, field->getLastText());
			box->closeBox();
		};
		field->submits() | rpl::on_next([=](Qt::KeyboardModifiers m) {
			if (!(m & Qt::ShiftModifier)) {
				save();
			}
		}, field->lifetime());
		box->setFocusCallback([=] {
			field->setFocusFast();
			field->setCursorPosition(field->getLastText().size());
		});
		box->addButton(tr::lng_settings_save(), save);
		if (!current.isEmpty()) {
			box->addLeftButton(tr::lng_tgn_delete(), [=] {
				SetChatNote(peer, QString());
				box->closeBox();
			});
		}
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

int ChatNoteChipWidth(const QString &note, int maxWidth) {
	if (note.isEmpty()) {
		return 0;
	}
	const auto &font = st::normalFont;
	const auto line = note.section('\n', 0, 0);
	const auto padding = font->height + font->spacew * 3;
	return std::min(font->width(line) + padding, maxWidth);
}

void PaintChatNoteChip(QPainter &p, QRect rect, const QString &note) {
	if (note.isEmpty() || rect.width() <= 0) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	const auto &font = st::normalFont;
	const auto r = QRectF(rect);
	const auto radius = r.height() / 2.;
	p.setPen(QPen(QColor(0xF5, 0xB7, 0x31, 150), 1.));
	p.setBrush(QColor(0xF5, 0xB7, 0x31, 60));
	p.drawRoundedRect(r.marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }), radius, radius);

	// A little folded sticker as the mark.
	const auto mark = r.height() * 0.42;
	const auto markRect = QRectF(
		r.x() + (r.height() - mark) / 2. + font->spacew * 0.5,
		r.y() + (r.height() - mark) / 2.,
		mark,
		mark);
	auto sticker = QPainterPath();
	const auto fold = mark * 0.35;
	sticker.moveTo(markRect.topLeft());
	sticker.lineTo(markRect.right(), markRect.top());
	sticker.lineTo(markRect.right(), markRect.bottom() - fold);
	sticker.lineTo(markRect.right() - fold, markRect.bottom());
	sticker.lineTo(markRect.left(), markRect.bottom());
	sticker.closeSubpath();
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xF5, 0xB7, 0x31));
	p.drawPath(sticker);

	const auto left = int(markRect.right() + font->spacew * 1.5);
	const auto available = rect.right() - left - font->spacew;
	p.setPen(st::windowFg);
	p.setFont(font);
	p.drawText(
		QRect(left, rect.y(), available, rect.height()),
		Qt::AlignLeft | Qt::AlignVCenter,
		font->elided(note.section('\n', 0, 0), available));
}

} // namespace Tristegram
