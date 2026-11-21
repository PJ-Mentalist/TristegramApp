/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_snapshot.h"

#include "core/application.h"
#include "core/file_utilities.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "extera/tristegram_logo.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text.h"
#include "ui/userpic_view.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"

#include <QtCore/QDateTime>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainterPath>

namespace Tristegram {
namespace {

constexpr auto kMaxMessages = 40;
constexpr auto kWidth = 520;
constexpr auto kPadding = 20;
constexpr auto kUserpic = 34;
constexpr auto kRadius = 16;
constexpr auto kMaxMediaHeight = 280;

[[nodiscard]] QColor Alpha(QColor color, double alpha) {
	color.setAlphaF(std::clamp(alpha, 0., 1.));
	return color;
}

struct Block {
	HistoryItem *item = nullptr;
	PeerData *from = nullptr;
	bool out = false;
	bool showName = false;
	bool showUserpic = false;
	bool gapBefore = false;
	QString name;
	QString time;
	Ui::Text::String text;
	QImage media;
	bool round = false;
	QString label;
	QSize mediaSize;
	int textWidth = 0;
	int textHeight = 0;
	int width = 0;
	int height = 0;
	Ui::PeerUserpicView userpic;
};

[[nodiscard]] QImage Original(Image *image) {
	return image ? image->original() : QImage();
}

// The picture of a photo or a video if it is already on this device,
// otherwise a short label for it.
void FillMedia(Block &block) {
	const auto media = block.item->media();
	if (!media) {
		return;
	}
	if (const auto photo = media->photo()) {
		const auto view = photo->createMediaView();
		for (const auto size : {
				Data::PhotoSize::Large,
				Data::PhotoSize::Thumbnail,
				Data::PhotoSize::Small }) {
			if (const auto image = view->image(size)) {
				block.media = image->original();
				break;
			}
		}
		if (block.media.isNull()) {
			block.media = Original(view->thumbnailInline());
		}
		if (block.media.isNull()) {
			block.label = tr::lng_tgsn_photo(tr::now);
		}
	} else if (const auto document = media->document()) {
		if (const auto sticker = document->sticker()) {
			block.label = tr::lng_tgsn_sticker(tr::now)
				+ (sticker->alt.isEmpty() ? QString() : (' ' + sticker->alt));
		} else if (document->isVideoFile()
			|| document->isVideoMessage()
			|| document->isAnimation()) {
			const auto view = document->createMediaView();
			block.media = Original(view->thumbnail());
			if (block.media.isNull()) {
				block.media = Original(view->thumbnailInline());
			}
			block.round = document->isVideoMessage();
			if (block.media.isNull()) {
				block.label = tr::lng_tgsn_video(tr::now);
			}
		} else if (document->isVoiceMessage()) {
			block.label = tr::lng_tgsn_voice(tr::now);
		} else if (const auto song = document->song()) {
			const auto title = song->title.isEmpty()
				? document->filename()
				: song->title;
			block.label = QString::fromUtf8("\xf0\x9f\x8e\xb5 ")
				+ (song->performer.isEmpty()
					? title
					: (song->performer + u" — "_q + title));
		} else {
			block.label = QString::fromUtf8("\xf0\x9f\x93\x8e ")
				+ document->filename();
		}
	}
}

[[nodiscard]] QString TimeText(TimeId date) {
	return QLocale().toString(
		QDateTime::fromSecsSinceEpoch(date).time(),
		QLocale::ShortFormat);
}

void PaintBackground(QPainter &p, QRect rect) {
	auto background = Window::Theme::Background()->createCurrentImage();
	if (background.isNull()) {
		const auto colors = CurrentLogoStyle();
		auto fill = QLinearGradient(rect.topLeft(), rect.bottomRight());
		fill.setColorAt(0., colors.from);
		fill.setColorAt(1., colors.to);
		p.fillRect(rect, fill);
		return;
	}
	const auto scale = std::max(
		rect.width() / double(background.width()),
		rect.height() / double(background.height()));
	const auto size = QSizeF(background.size()) * scale;
	p.drawImage(
		QRectF(
			rect.x() + (rect.width() - size.width()) / 2.,
			rect.y() + (rect.height() - size.height()) / 2.,
			size.width(),
			size.height()),
		background);
}

[[nodiscard]] QImage Render(
		not_null<History*> history,
		std::vector<Block> &blocks) {
	const auto timeFont = st::msgDateFont;
	const auto nameFont = st::msgNameFont;
	const auto padding = st::msgPadding;
	const auto groups = !history->peer->isUser();
	const auto bubbleMax = kWidth
		- 2 * kPadding
		- (groups ? (kUserpic + 8) : 0)
		- 40;

	// Measure.
	auto total = kPadding + 52;
	for (auto &block : blocks) {
		const auto inner = bubbleMax - padding.left() - padding.right();
		block.textWidth = 0;
		block.textHeight = 0;
		const auto timeWidth = timeFont->width(block.time) + 6;
		if (!block.text.isEmpty()) {
			block.text.updateSkipBlock(timeWidth, timeFont->height);
			block.textWidth = std::min(block.text.maxWidth(), inner);
			block.textHeight = block.text.countHeight(block.textWidth);
		}
		auto content = block.textWidth;
		if (block.showName) {
			content = std::max(
				content,
				std::min(nameFont->width(block.name), inner));
		}
		if (!block.media.isNull()) {
			const auto image = block.media.size();
			auto size = QSize(inner, kMaxMediaHeight);
			if (block.round) {
				size = QSize(200, 200);
			} else {
				size = image.scaled(size, Qt::KeepAspectRatio);
				if (size.width() < 120) {
					size = image.scaled(QSize(120, kMaxMediaHeight), Qt::KeepAspectRatio);
				}
			}
			block.mediaSize = size;
			content = std::max(content, size.width());
		}
		if (!block.label.isEmpty()) {
			content = std::max(
				content,
				std::min(st::normalFont->width(block.label), inner));
		}
		if (block.text.isEmpty()) {
			content = std::max(content, timeWidth);
		}
		block.width = content + padding.left() + padding.right();
		auto height = padding.top() + padding.bottom();
		if (block.showName) {
			height += nameFont->height + 2;
		}
		if (!block.mediaSize.isEmpty()) {
			height += block.mediaSize.height() + 6;
		}
		if (!block.label.isEmpty()) {
			height += st::normalFont->height + 4;
		}
		height += block.text.isEmpty()
			? timeFont->height
			: block.textHeight;
		block.height = height;
		total += (block.gapBefore ? 12 : 4) + height;
	}
	total += kPadding + 18;

	const auto ratio = 2;
	auto result = QImage(
		QSize(kWidth, total) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto p = Painter(&result);
	auto hq = PainterHighQualityEnabler(p);
	const auto full = QRect(0, 0, kWidth, total);
	auto clip = QPainterPath();
	clip.addRoundedRect(full, 24, 24);
	p.setClipPath(clip);
	PaintBackground(p, full);

	// The chat name on a glass pill at the top.
	const auto colors = CurrentLogoStyle();
	{
		const auto title = history->peer->name();
		const auto font = st::semiboldFont;
		const auto width = std::min(font->width(title) + 32, kWidth - 2 * kPadding);
		const auto pill = QRectF(
			(kWidth - width) / 2.,
			kPadding,
			width,
			32);
		p.setPen(Qt::NoPen);
		p.setBrush(Alpha(st::windowBg->c, 0.78));
		p.drawRoundedRect(pill, 16, 16);
		p.setFont(font);
		p.setPen(st::windowBoldFg);
		p.drawText(
			pill,
			Qt::AlignCenter,
			font->elided(title, int(width) - 24));
	}

	auto top = kPadding + 52;
	for (auto &block : blocks) {
		top += block.gapBefore ? 12 : 4;
		const auto left = block.out
			? (kWidth - kPadding - block.width)
			: (kPadding + (groups ? (kUserpic + 8) : 0));
		const auto bubble = QRect(left, top, block.width, block.height);
		if (block.showUserpic && block.from) {
			block.from->paintUserpic(
				p,
				block.userpic,
				kPadding,
				bubble.bottom() - kUserpic + 1,
				kUserpic);
		}
		p.setPen(Qt::NoPen);
		p.setBrush(block.out ? st::msgOutBg : st::msgInBg);
		p.drawRoundedRect(bubble, kRadius, kRadius);

		auto y = top + padding.top();
		const auto x = left + padding.left();
		const auto inner = block.width - padding.left() - padding.right();
		if (block.showName) {
			p.setFont(nameFont);
			p.setPen(colors.from);
			p.drawText(
				QRect(x, y, inner, nameFont->height),
				Qt::AlignLeft | Qt::AlignVCenter,
				nameFont->elided(block.name, inner));
			y += nameFont->height + 2;
		}
		if (!block.mediaSize.isEmpty()) {
			const auto rect = QRect(
				x + (inner - block.mediaSize.width()) / 2,
				y,
				block.mediaSize.width(),
				block.mediaSize.height());
			auto path = QPainterPath();
			if (block.round) {
				path.addEllipse(rect);
			} else {
				path.addRoundedRect(rect, 10, 10);
			}
			p.save();
			p.setClipPath(path, Qt::IntersectClip);
			const auto scale = std::max(
				rect.width() / double(block.media.width()),
				rect.height() / double(block.media.height()));
			const auto size = QSizeF(block.media.size()) * scale;
			p.drawImage(
				QRectF(
					rect.x() + (rect.width() - size.width()) / 2.,
					rect.y() + (rect.height() - size.height()) / 2.,
					size.width(),
					size.height()),
				block.media);
			p.restore();
			y += block.mediaSize.height() + 6;
		}
		if (!block.label.isEmpty()) {
			p.setFont(st::normalFont);
			p.setPen(block.out ? st::historyTextOutFg : st::historyTextInFg);
			p.drawText(
				QRect(x, y, inner, st::normalFont->height),
				Qt::AlignLeft | Qt::AlignVCenter,
				st::normalFont->elided(block.label, inner));
			y += st::normalFont->height + 4;
		}
		if (!block.text.isEmpty()) {
			p.setPen(block.out ? st::historyTextOutFg : st::historyTextInFg);
			block.text.drawLeft(p, x, y, block.textWidth, kWidth);
		}
		p.setFont(timeFont);
		p.setPen(block.out ? st::msgOutDateFg : st::msgInDateFg);
		const auto timeWidth = timeFont->width(block.time);
		p.drawText(
			bubble.right() - padding.right() - timeWidth + 2,
			bubble.bottom() - padding.bottom() + 2 - timeFont->height + timeFont->ascent,
			block.time);
		top += block.height;
	}

	// A small mark in the corner.
	p.setFont(st::normalFont);
	p.setPen(Alpha(colors.to, 0.85));
	p.drawText(
		QRect(0, total - kPadding - 6, kWidth - kPadding, 16),
		Qt::AlignRight | Qt::AlignVCenter,
		u"Tristegram"_q);
	return result;
}

class Preview final : public Ui::RpWidget {
public:
	Preview(QWidget *parent, QImage image)
	: RpWidget(parent)
	, _image(std::move(image)) {
	}

protected:
	int resizeGetHeight(int newWidth) override {
		const auto size = _image.size() / _image.devicePixelRatio();
		const auto scale = std::min(
			1.,
			std::min(
				(newWidth - 2 * st::boxPadding.left()) / double(size.width()),
				460. / size.height()));
		_shown = QSize(
			int(size.width() * scale),
			int(size.height() * scale));
		return _shown.height() + st::boxPadding.bottom();
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(
			QRect(QPoint((width() - _shown.width()) / 2, 0), _shown),
			_image);
	}

private:
	QImage _image;
	QSize _shown;

};

} // namespace

void ShowChatSnapshot(
		not_null<Window::SessionController*> controller,
		MessageIdsList ids) {
	const auto session = &controller->session();
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto &id : ids) {
		if (const auto item = session->data().message(id)) {
			items.push_back(item);
		}
	}
	if (items.empty()) {
		return;
	}
	ranges::sort(items, [](not_null<HistoryItem*> a, not_null<HistoryItem*> b) {
		return (a->date() != b->date())
			? (a->date() < b->date())
			: (a->id < b->id);
	});
	if (int(items.size()) > kMaxMessages) {
		items.erase(items.begin(), items.end() - kMaxMessages);
	}
	const auto history = items.front()->history();
	const auto groups = !history->peer->isUser();
	auto blocks = std::vector<Block>();
	blocks.reserve(items.size());
	for (auto i = 0; i != int(items.size()); ++i) {
		const auto item = items[i];
		const auto from = item->from().get();
		const auto previous = (i > 0) ? items[i - 1]->from().get() : nullptr;
		const auto next = (i + 1 < int(items.size()))
			? items[i + 1]->from().get()
			: nullptr;
		auto block = Block();
		block.item = item;
		block.from = from;
		block.out = item->out();
		block.gapBefore = (i > 0) && (previous != from);
		block.showName = groups && !block.out && (previous != from);
		block.showUserpic = groups && !block.out && (next != from);
		block.name = from ? from->name() : QString();
		block.time = TimeText(item->date());
		const auto text = item->originalText();
		if (!text.text.isEmpty()) {
			block.text = Ui::Text::String(
				st::messageTextStyle,
				text,
				kMarkupTextOptions);
		}
		FillMedia(block);
		blocks.push_back(std::move(block));
	}
	const auto image = Render(history, blocks);
	const auto copy = [=] {
		QGuiApplication::clipboard()->setImage(image);
		controller->showToast(tr::lng_tgsn_copied(tr::now));
	};
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgsn_title());
		box->setWidth(st::boxWideWidth);
		box->addRow(
			object_ptr<Preview>(box, image),
			QMargins());
		box->addButton(tr::lng_tgsn_copy(), [=] {
			copy();
			box->closeBox();
		});
		box->addButton(tr::lng_tgsn_save(), [=] {
			FileDialog::GetWritePath(
				Core::App().getFileDialogParent(),
				tr::lng_tgsn_save(tr::now),
				u"PNG (*.png)"_q,
				u"Tristegram "_q
					+ QDateTime::currentDateTime().toString(
						u"yyyy-MM-dd HH-mm-ss"_q)
					+ u".png"_q,
				[=](QString &&path) {
					if (!path.isEmpty() && image.save(path, "PNG")) {
						controller->showToast(tr::lng_tgsn_saved(tr::now));
					}
				});
		});
		box->addLeftButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace Tristegram
