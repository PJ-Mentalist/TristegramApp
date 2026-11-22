/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_song_edit.h"

#include "base/random.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "extera/extera_config.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QPainterPath>

namespace Tristegram {
namespace {

constexpr auto kMaxLength = 128;
constexpr auto kCoverSize = 800;

[[nodiscard]] QString Key() {
	return u"tristegram_song_edits"_q;
}

// Parsed once and again only after a change: song names are asked for
// while painting.
[[nodiscard]] std::optional<QJsonObject> &Cached() {
	static auto result = std::optional<QJsonObject>();
	return result;
}

[[nodiscard]] const QJsonObject &All() {
	auto &cached = Cached();
	static auto subscribed = false;
	if (!std::exchange(subscribed, true)) {
		static auto lifetime = rpl::lifetime();
		Extera::Config::Instance().changes(
		) | rpl::filter([](const QString &key) {
			return key == Key();
		}) | rpl::on_next([] {
			Cached().reset();
		}, lifetime);
	}
	if (!cached) {
		cached = QJsonDocument::fromJson(
			Extera::String(Key()).toUtf8()).object();
	}
	return *cached;
}

void SaveAll(const QJsonObject &all) {
	Cached() = all;
	Extera::Config::Instance().setString(
		Key(),
		QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact)));
}

[[nodiscard]] base::flat_map<QString, QImage> &CoverCache() {
	static auto result = base::flat_map<QString, QImage>();
	return result;
}

[[nodiscard]] QImage LoadCover(const QString &path) {
	if (path.isEmpty()) {
		return QImage();
	}
	auto &cache = CoverCache();
	const auto i = cache.find(path);
	if (i != end(cache)) {
		return i->second;
	}
	auto image = QImage(path);
	if (!image.isNull()) {
		image = std::move(image).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	}
	if (cache.size() > 32) {
		cache.clear();
	}
	cache.emplace(path, image);
	return image;
}

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

// The cover in the box: a click chooses another picture.
class CoverPicker final : public Ui::RippleButton {
public:
	CoverPicker(QWidget *parent, Fn<QImage()> image)
	: RippleButton(parent, st::defaultRippleAnimation)
	, _image(std::move(image)) {
		resize(Px(132), Px(132));
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto r = QRectF(rect()).marginsRemoved(
			QMarginsF(1., 1., 1., 1.));
		const auto radius = double(Px(18));
		auto clip = QPainterPath();
		clip.addRoundedRect(r, radius, radius);
		p.setClipPath(clip);
		const auto image = _image();
		if (!image.isNull()) {
			const auto side = std::min(image.width(), image.height());
			const auto ratio = style::DevicePixelRatio();
			const auto scaled = image.copy(
				(image.width() - side) / 2,
				(image.height() - side) / 2,
				side,
				side).scaled(
					int(r.width() * ratio),
					int(r.height() * ratio),
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
			p.drawImage(r, scaled);
		} else {
			auto fill = QLinearGradient(r.topLeft(), r.bottomRight());
			fill.setColorAt(0., QColor(0x5B, 0x5C, 0xF6));
			fill.setColorAt(1., QColor(0xE8, 0x31, 0x8A));
			p.fillRect(r, fill);
		}
		// A dark veil with a camera while the mouse is over it.
		const auto over = isOver() || image.isNull();
		if (over) {
			p.fillRect(r, QColor(0, 0, 0, image.isNull() ? 40 : 110));
			const auto c = r.center();
			const auto w = r.width() * 0.3;
			const auto h = w * 0.72;
			const auto body = QRectF(c.x() - w / 2, c.y() - h / 2, w, h);
			p.setPen(QPen(QColor(255, 255, 255), Px(2)));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(body, w * 0.14, w * 0.14);
			p.drawEllipse(c, h * 0.27, h * 0.27);
			p.drawLine(
				QPointF(body.x() + w * 0.3, body.y()),
				QPointF(body.x() + w * 0.38, body.y() - h * 0.16));
			p.drawLine(
				QPointF(body.x() + w * 0.38, body.y() - h * 0.16),
				QPointF(body.x() + w * 0.62, body.y() - h * 0.16));
			p.drawLine(
				QPointF(body.x() + w * 0.62, body.y() - h * 0.16),
				QPointF(body.x() + w * 0.7, body.y()));
		}
		paintRipple(p, 0, 0);
		p.setClipping(false);
		p.setPen(QPen(QColor(255, 255, 255, 30), 1.));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(r, radius, radius);
	}
	void onStateChanged(State was, StateChangeSource source) override {
		RippleButton::onStateChanged(was, source);
		update();
	}
	QImage prepareRippleMask() const override {
		return Ui::RippleAnimation::RoundRectMask(size(), Px(18));
	}

private:
	Fn<QImage()> _image;

};

} // namespace

std::optional<SongEdit> SongEditFor(uint64 documentId) {
	if (!documentId) {
		return std::nullopt;
	}
	const auto &all = All();
	if (all.isEmpty()) {
		return std::nullopt;
	}
	const auto i = all.find(QString::number(documentId));
	if (i == all.end()) {
		return std::nullopt;
	}
	const auto object = i->toObject();
	return SongEdit{
		.title = object.value(u"t"_q).toString(),
		.performer = object.value(u"a"_q).toString(),
		.cover = object.value(u"c"_q).toString(),
	};
}

rpl::producer<> SongEditsChanged() {
	return Extera::Config::Instance().changes(
	) | rpl::filter([](const QString &key) {
		return key == Key();
	}) | rpl::to_empty;
}

QImage SongEditCover(uint64 documentId) {
	const auto edit = SongEditFor(documentId);
	return edit ? LoadCover(edit->cover) : QImage();
}

QString SaveSquareCover(const QImage &original, int size) {
	if (original.isNull()) {
		return QString();
	}
	const auto side = std::min(original.width(), original.height());
	const auto target = std::min(side, size);
	const auto square = original.copy(
		(original.width() - side) / 2,
		(original.height() - side) / 2,
		side,
		side).scaled(
			target,
			target,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
	const auto folder = cWorkingDir() + u"tdata/tristegram_covers/"_q;
	QDir().mkpath(folder);
	const auto path = folder
		+ QString::number(base::RandomValue<uint64>(), 16)
		+ u".jpg"_q;
	return square.save(path, "JPG", 92) ? path : QString();
}

void EditSong(
		not_null<Window::SessionController*> controller,
		not_null<DocumentData*> document) {
	const auto id = document->id;
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgse_title());
		box->setWidth(st::boxWideWidth);

		struct State {
			std::shared_ptr<Data::DocumentMedia> media;
			QString cover;
			bool coverChanged = false;
			QImage picked;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto edit = SongEditFor(id);
		state->cover = edit ? edit->cover : QString();
		state->media = document->createMediaView();
		state->media->thumbnailWanted(Data::FileOrigin());

		const auto original = document->song();
		auto originalTitle = original ? original->title : QString();
		auto originalPerformer = original ? original->performer : QString();
		if (originalTitle.isEmpty()) {
			originalTitle = document->filename();
			const auto dot = originalTitle.lastIndexOf('.');
			if (dot > 0) {
				originalTitle = originalTitle.mid(0, dot);
			}
		}

		const auto current = [=]() -> QImage {
			if (!state->picked.isNull()) {
				return state->picked;
			} else if (!state->cover.isEmpty()) {
				return LoadCover(state->cover);
			} else if (const auto image = state->media->thumbnail()) {
				return image->original();
			}
			return QImage();
		};

		const auto wrap = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::boxRowPadding + QMargins(0, Px(6), 0, Px(10)));
		wrap->resize(wrap->width(), Px(132));
		const auto picker = Ui::CreateChild<CoverPicker>(wrap, current);
		wrap->widthValue(
		) | rpl::on_next([=](int width) {
			picker->move((width - picker->width()) / 2, 0);
		}, wrap->lifetime());
		document->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			picker->update();
		}, picker->lifetime());

		const auto choose = [=] {
			FileDialog::GetOpenPath(
				Core::App().getFileDialogParent(),
				tr::lng_tgse_cover_choose(tr::now),
				FileDialog::ImagesOrAllFilter(),
				crl::guard(box, [=](FileDialog::OpenResult &&result) {
					auto image = !result.remoteContent.isEmpty()
						? QImage::fromData(result.remoteContent)
						: result.paths.isEmpty()
						? QImage()
						: QImage(result.paths.front());
					if (image.isNull()) {
						box->showToast(tr::lng_tgm_cover_failed(tr::now));
						return;
					}
					state->picked = std::move(image).convertToFormat(
						QImage::Format_ARGB32_Premultiplied);
					state->coverChanged = true;
					picker->update();
				}));
		};
		picker->setClickedCallback(choose);

		const auto links = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::boxRowPadding + QMargins(0, 0, 0, Px(8)));
		links->resize(links->width(), st::defaultLinkButton.font->height);
		const auto change = Ui::CreateChild<Ui::LinkButton>(
			links,
			tr::lng_tgse_cover_choose(tr::now));
		const auto remove = Ui::CreateChild<Ui::LinkButton>(
			links,
			tr::lng_tgse_cover_remove(tr::now));
		change->setClickedCallback(choose);
		remove->setClickedCallback([=] {
			state->picked = QImage();
			state->cover = QString();
			state->coverChanged = true;
			picker->update();
		});
		links->widthValue(
		) | rpl::on_next([=](int width) {
			const auto gap = Px(18);
			const auto total = change->width() + gap + remove->width();
			change->move((width - total) / 2, 0);
			remove->move(change->x() + change->width() + gap, 0);
		}, links->lifetime());

		const auto title = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_tgse_name(),
			(edit && !edit->title.isEmpty()) ? edit->title : originalTitle));
		title->setMaxLength(kMaxLength);
		const auto performer = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_tgse_artist(),
			(edit && !edit->performer.isEmpty())
				? edit->performer
				: originalPerformer));
		performer->setMaxLength(kMaxLength);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_tgse_about(),
			st::boxDividerLabel),
			st::boxRowPadding + QMargins(0, Px(10), 0, 0));

		box->setFocusCallback([=] { title->setFocusFast(); });

		const auto save = [=] {
			auto cover = state->cover;
			if (state->coverChanged && !state->picked.isNull()) {
				cover = SaveSquareCover(state->picked, kCoverSize);
				if (cover.isEmpty()) {
					box->showToast(tr::lng_tgm_cover_failed(tr::now));
					return;
				}
			}
			if (edit
				&& !edit->cover.isEmpty()
				&& edit->cover != cover) {
				QFile::remove(edit->cover);
				CoverCache().remove(edit->cover);
			}
			auto newTitle = title->getLastText().trimmed();
			auto newPerformer = performer->getLastText().trimmed();
			if (newTitle == originalTitle) {
				newTitle = QString();
			}
			if (newPerformer == originalPerformer) {
				newPerformer = QString();
			}
			auto all = QJsonObject(All());
			const auto key = QString::number(id);
			if (newTitle.isEmpty()
				&& newPerformer.isEmpty()
				&& cover.isEmpty()) {
				all.remove(key);
			} else {
				auto object = QJsonObject();
				if (!newTitle.isEmpty()) {
					object.insert(u"t"_q, newTitle);
				}
				if (!newPerformer.isEmpty()) {
					object.insert(u"a"_q, newPerformer);
				}
				if (!cover.isEmpty()) {
					object.insert(u"c"_q, cover);
				}
				all.insert(key, object);
			}
			SaveAll(all);
			box->closeBox();
		};
		title->submits() | rpl::on_next([=] {
			performer->setFocus();
		}, title->lifetime());
		performer->submits() | rpl::on_next(save, performer->lifetime());

		box->addButton(tr::lng_settings_save(), save);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		if (edit) {
			box->addLeftButton(tr::lng_tgse_reset(), [=] {
				if (!edit->cover.isEmpty()) {
					QFile::remove(edit->cover);
					CoverCache().remove(edit->cover);
				}
				auto all = QJsonObject(All());
				all.remove(QString::number(id));
				SaveAll(all);
				box->closeBox();
			});
		}
	}));
}

} // namespace Tristegram
