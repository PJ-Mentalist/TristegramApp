/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/message_bubble.h"

#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "ui/painter.h"
#include <QtGui/QPainterPath>

#include "ui/cached_round_corners.h"
#include "ui/image/image_prepare.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/torn_edge.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"

namespace Ui {
namespace {

using Corner = BubbleCornerRounding;

[[nodiscard]] bool UsePatternBubble(const SimpleBubble &args) {
	return !args.selected
		&& args.outbg
		&& args.pattern
		&& !args.patternViewport.isEmpty()
		&& !args.pattern->pixmap.size().isEmpty();
}

[[nodiscard]] float64 PatternBubbleOpacity(
		const SimpleBubble &args,
		float64 wasOpacity) {
	return args.st->msgOutBg()->c.alphaF() * wasOpacity;
}

void PaintBubblePiece(
		QPainter &p,
		const SimpleBubble &args,
		QRect geometry,
		bool fromTop,
		bool tillBottom) {
	auto simple = args;
	simple.geometry = geometry;
	if (!fromTop) {
		simple.rounding.topLeft
			= simple.rounding.topRight
			= Corner::None;
	}
	if (!tillBottom) {
		simple.rounding.bottomLeft
			= simple.rounding.bottomRight
			= Corner::None;
		simple.shadowed = false;
	}
	PaintBubble(p, simple);
}

void PaintBubblePiece(
		QPainter &p,
		const SimpleBubble &args,
		QRect geometry,
		bool selected,
		bool fromTop,
		bool tillBottom) {
	auto simple = args;
	simple.selected = selected;
	PaintBubblePiece(p, simple, geometry, fromTop, tillBottom);
}

template <
	typename FillBg, // fillBg(QRect rect)
	typename FillSh, // fillSh(QRect rect)
	typename FillCorner, // fillCorner(int x, int y, int index, Corner size)
	typename PaintTail> // paintTail(QPoint bottomPosition) -> tailWidth
void PaintBubbleGeneric(
		const SimpleBubble &args,
		FillBg &&fillBg,
		FillSh &&fillSh,
		FillCorner &&fillCorner,
		PaintTail &&paintTail) {
	using namespace Images;

	const auto topLeft = args.rounding.topLeft;
	const auto topRight = args.rounding.topRight;
	const auto bottomWithTailLeft = args.rounding.bottomLeft;
	const auto bottomWithTailRight = args.rounding.bottomRight;
	if (topLeft == Corner::None
		&& topRight == Corner::None
		&& bottomWithTailLeft == Corner::None
		&& bottomWithTailRight == Corner::None) {
		fillBg(args.geometry);
		return;
	}
	const auto bottomLeft = (bottomWithTailLeft == Corner::Tail)
		? Corner::None
		: bottomWithTailLeft;
	const auto bottomRight = (bottomWithTailRight == Corner::Tail)
		? Corner::None
		: bottomWithTailRight;
	const auto rect = args.geometry;
	const auto small = BubbleRadiusSmall();
	const auto large = BubbleRadiusLarge();
	const auto cornerSize = [&](Corner corner) {
		return (corner == Corner::Large)
			? large
			: (corner == Corner::Small)
			? small
			: 0;
	};
	const auto verticalSkip = [&](Corner left, Corner right) {
		return std::max(cornerSize(left), cornerSize(right));
	};
	const auto top = verticalSkip(topLeft, topRight);
	const auto bottom = verticalSkip(bottomLeft, bottomRight);
	if (top) {
		const auto left = cornerSize(topLeft);
		const auto right = cornerSize(topRight);
		if (left) {
			fillCorner(rect.left(), rect.top(), kTopLeft, topLeft);
			if (const auto add = top - left) {
				fillBg({ rect.left(), rect.top() + left, left, add });
			}
		}
		if (const auto fill = rect.width() - left - right; fill > 0) {
			fillBg({ rect.left() + left, rect.top(), fill, top });
		}
		if (right) {
			fillCorner(
				rect.left() + rect.width() - right,
				rect.top(),
				kTopRight,
				topRight);
			if (const auto add = top - right) {
				fillBg({
					rect.left() + rect.width() - right,
					rect.top() + right,
					right,
					add,
				});
			}
		}
	}
	if (const auto fill = rect.height() - top - bottom; fill > 0) {
		fillBg({ rect.left(), rect.top() + top, rect.width(), fill });
	}
	if (bottom) {
		const auto left = cornerSize(bottomLeft);
		const auto right = cornerSize(bottomRight);
		if (left) {
			fillCorner(
				rect.left(),
				rect.top() + rect.height() - left,
				kBottomLeft,
				bottomLeft);
			if (const auto add = bottom - left) {
				fillBg({
					rect.left(),
					rect.top() + rect.height() - bottom,
					left,
					add,
				});
			}
		}
		if (const auto fill = rect.width() - left - right; fill > 0) {
			fillBg({
				rect.left() + left,
				rect.top() + rect.height() - bottom,
				fill,
				bottom,
			});
		}
		if (right) {
			fillCorner(
				rect.left() + rect.width() - right,
				rect.top() + rect.height() - right,
				kBottomRight,
				bottomRight);
			if (const auto add = bottom - right) {
				fillBg({
					rect.left() + rect.width() - right,
					rect.top() + rect.height() - bottom,
					right,
					add,
				});
			}
		}
	}
	const auto leftTail = (bottomWithTailLeft == Corner::Tail)
		? paintTail({ rect.x(), rect.y() + rect.height() })
		: 0;
	const auto rightTail = (bottomWithTailRight == Corner::Tail)
		? paintTail({ rect.x() + rect.width(), rect.y() + rect.height() })
		: 0;
	if (!args.shadowed) {
		return;
	}
	const auto shLeft = rect.x() + cornerSize(bottomLeft) - leftTail;
	const auto shWidth = rect.x()
		+ rect.width()
		- cornerSize(bottomRight)
		+ rightTail
		- shLeft;
	if (shWidth > 0) {
		fillSh({ shLeft, rect.y() + rect.height(), shWidth, st::msgShadow });
	}
}

void PaintPatternBubble(QPainter &p, const SimpleBubble &args) {
	const auto wasOpacity = p.opacity();
	const auto opacity = PatternBubbleOpacity(args, wasOpacity);
	const auto shadowOpacity = opacity * args.st->msgOutShadow()->c.alphaF();
	const auto pattern = args.pattern;
	const auto &tail = (args.rounding.bottomRight == Corner::Tail)
		? pattern->tailRight
		: pattern->tailLeft;
	const auto tailShift = (args.rounding.bottomRight == Corner::Tail
		? QPoint(0, tail.height())
		: QPoint(tail.width(), tail.height())) / int(tail.devicePixelRatio());
	const auto fillBg = [&](const QRect &rect) {
		const auto fill = rect.intersected(args.patternViewport);
		if (!fill.isEmpty()) {
			PaintPatternBubblePart(
				p,
				args.patternViewport,
				pattern->pixmap,
				fill);
		}
	};
	const auto fillSh = [&](const QRect &rect) {
		p.setOpacity(shadowOpacity);
		fillBg(rect);
		p.setOpacity(opacity);
	};
	const auto fillPattern = [&](
			int x,
			int y,
			const QImage &mask,
			QImage &cache) {
		PaintPatternBubblePart(
			p,
			args.patternViewport,
			pattern->pixmap,
			QRect(QPoint(x, y), mask.size() / int(mask.devicePixelRatio())),
			mask,
			cache);
	};
	const auto fillCorner = [&](int x, int y, int index, Corner size) {
		auto &corner = (size == Corner::Large)
			? pattern->cornersLarge[index]
			: pattern->cornersSmall[index];
		auto &cache = (size == Corner::Large)
			? (index < 2
				? pattern->cornerTopLargeCache
				: pattern->cornerBottomLargeCache)
			: (index < 2
				? pattern->cornerTopSmallCache
				: pattern->cornerBottomSmallCache);
		fillPattern(x, y, corner, cache);
	};
	const auto paintTail = [&](QPoint bottomPosition) {
		if (tail.isNull()) {
			// Tristegram: the tail is not prepared yet, skip it this time.
			return 0;
		}
		const auto position = bottomPosition - tailShift;
		fillPattern(position.x(), position.y(), tail, pattern->tailCache);
		return tail.width() / int(tail.devicePixelRatio());
	};

	p.setOpacity(opacity);
	PaintBubbleGeneric(args, fillBg, fillSh, fillCorner, paintTail);
	p.setOpacity(wasOpacity);
}

void PaintSolidBubble(QPainter &p, const SimpleBubble &args) {
	const auto &st = args.st->messageStyle(args.outbg, args.selected);
	const auto &bg = st.msgBg;
	const auto sh = (args.rounding.bottomRight == Corner::None)
		? nullptr
		: &st.msgShadow;
	const auto &tail = (args.rounding.bottomRight == Corner::Tail)
		? st.tailRight
		: st.tailLeft;
	const auto tailShift = (args.rounding.bottomRight == Corner::Tail)
		? QPoint(0, tail.height())
		: QPoint(tail.width(), tail.height());

	PaintBubbleGeneric(args, [&](const QRect &rect) {
		p.fillRect(rect, bg);
	}, [&](const QRect &rect) {
		p.fillRect(rect, *sh);
	}, [&](int x, int y, int index, Corner size) {
		auto &corners = (size == Corner::Large)
			? st.msgBgCornersLarge
			: st.msgBgCornersSmall;
		p.drawPixmap(x, y, corners.p[index]);
	}, [&](const QPoint &bottomPosition) {
		tail.paint(p, bottomPosition - tailShift, args.outerWidth);
		return tail.width();
	});
}

} // namespace

std::unique_ptr<BubblePattern> PrepareBubblePattern(
		not_null<const style::palette*> st) {
	auto result = std::make_unique<Ui::BubblePattern>();
	result->cornersSmall = Images::CornersMask(BubbleRadiusSmall());
	result->cornersLarge = Images::CornersMask(BubbleRadiusLarge());
	const auto addShadow = [&](QImage &bottomCorner) {
		auto result = QImage(
			bottomCorner.width(),
			(bottomCorner.height()
				+ st::msgShadow * int(bottomCorner.devicePixelRatio())),
			QImage::Format_ARGB32_Premultiplied);
		result.fill(Qt::transparent);
		result.setDevicePixelRatio(bottomCorner.devicePixelRatio());
		auto p = QPainter(&result);
		p.setOpacity(st->msgInShadow()->c.alphaF());
		p.drawImage(0, st::msgShadow, bottomCorner);
		p.setOpacity(1.);
		p.drawImage(0, 0, bottomCorner);
		p.end();

		bottomCorner = std::move(result);
	};
	addShadow(result->cornersSmall[2]);
	addShadow(result->cornersSmall[3]);
	result->cornerTopSmallCache = QImage(
		result->cornersSmall[0].size(),
		QImage::Format_ARGB32_Premultiplied);
	result->cornerTopLargeCache = QImage(
		result->cornersLarge[0].size(),
		QImage::Format_ARGB32_Premultiplied);
	result->cornerBottomSmallCache = QImage(
		result->cornersSmall[2].size(),
		QImage::Format_ARGB32_Premultiplied);
	result->cornerBottomLargeCache = QImage(
		result->cornersLarge[2].size(),
		QImage::Format_ARGB32_Premultiplied);
	return result;
}

void FinishBubblePatternOnMain(not_null<BubblePattern*> pattern) {
	pattern->tailLeft = st::historyBubbleTailOutLeft.instance(Qt::white);
	pattern->tailRight = st::historyBubbleTailOutRight.instance(Qt::white);
	pattern->tailCache = QImage(
		pattern->tailLeft.size(),
		QImage::Format_ARGB32_Premultiplied);
}

// Tristegram: bubble looks, "tristegram_bubble_style":
// 1 glass, 2 neon, 3 paper, 4 pixel.
[[nodiscard]] bool PaintStyledBubble(QPainter &p, const SimpleBubble &args) {
	const auto style = Extera::Int(u"tristegram_bubble_style"_q);
	if (style <= 0 || style > 4 || args.selected) {
		return false;
	}
	const auto &stm = args.st->messageStyle(args.outbg, false);
	const auto base = stm.msgBg->c;
	const auto rect = QRectF(args.geometry);
	const auto radius = double(st::bubbleRadiusLarge);
	auto hq = PainterHighQualityEnabler(p);
	p.save();
	p.setPen(Qt::NoPen);
	switch (style) {
	case 1: { // Glass: the background shows through, a light rim.
		auto fill = base;
		fill.setAlphaF(0.55);
		p.setBrush(fill);
		p.drawRoundedRect(rect, radius, radius);
		auto sheen = QLinearGradient(rect.topLeft(), rect.bottomLeft());
		sheen.setColorAt(0., QColor(255, 255, 255, 46));
		sheen.setColorAt(0.45, QColor(255, 255, 255, 0));
		p.setBrush(sheen);
		p.drawRoundedRect(rect, radius, radius);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(255, 255, 255, 50), 1.));
		p.drawRoundedRect(rect.marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }), radius, radius);
	} break;
	case 2: { // Neon: a dark body and a glowing outline.
		const auto logo = Tristegram::CurrentLogoStyle();
		auto glowColor = args.outbg ? logo.from : logo.to;
		for (auto i = 3; i != 0; --i) {
			auto c = glowColor;
			c.setAlphaF(0.08 * (4 - i));
			p.setBrush(Qt::NoBrush);
			p.setPen(QPen(c, 2. * i + 1.));
			p.drawRoundedRect(rect, radius, radius);
		}
		auto body = base.darker(135);
		body.setAlphaF(0.92);
		p.setPen(Qt::NoPen);
		p.setBrush(body);
		p.drawRoundedRect(rect, radius, radius);
		auto outline = QLinearGradient(rect.topLeft(), rect.bottomRight());
		outline.setColorAt(0., logo.from);
		outline.setColorAt(1., logo.to);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QBrush(outline), 1.6));
		p.drawRoundedRect(rect.marginsRemoved({ 0.8, 0.8, 0.8, 0.8 }), radius, radius);
	} break;
	case 3: { // Paper: a warm sheet with a soft shadow.
		const auto warm = QColor(0xE8, 0xD5, 0xA8);
		const auto mix = [&](int a, int b) { return a + (b - a) * 22 / 100; };
		const auto paper = QColor(
			mix(base.red(), warm.red()),
			mix(base.green(), warm.green()),
			mix(base.blue(), warm.blue()));
		p.setBrush(QColor(0, 0, 0, 38));
		p.drawRoundedRect(rect.translated(0., 2.), radius * 0.4, radius * 0.4);
		p.setBrush(paper);
		p.drawRoundedRect(rect, radius * 0.4, radius * 0.4);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(0x8A, 0x6D, 0x3B, 60), 1.));
		p.drawRoundedRect(rect.marginsRemoved({ 0.5, 0.5, 0.5, 0.5 }), radius * 0.4, radius * 0.4);
		// A folded corner.
		const auto fold = std::min(rect.height(), rect.width()) * 0.18;
		auto corner = QPainterPath();
		corner.moveTo(rect.right() - fold, rect.top());
		corner.lineTo(rect.right(), rect.top() + fold);
		corner.lineTo(rect.right() - fold, rect.top() + fold);
		corner.closeSubpath();
		p.setPen(Qt::NoPen);
		p.setBrush(paper.darker(112));
		p.drawPath(corner);
	} break;
	case 4: { // Pixel: stepped corners and a hard shadow.
		const auto step = std::max(2., std::round(radius / 4.));
		const auto r = rect.toAlignedRect();
		auto shape = QPainterPath();
		const auto addShape = [&](QRectF b) {
			auto path = QPainterPath();
			path.addRect(b.adjusted(step * 2, 0, -step * 2, 0));
			path.addRect(b.adjusted(step, step, -step, -step));
			path.addRect(b.adjusted(0, step * 2, 0, -step * 2));
			return path.simplified();
		};
		p.setBrush(base.darker(170));
		p.drawPath(addShape(QRectF(r).translated(step, step)));
		shape = addShape(QRectF(r));
		p.setBrush(base);
		p.drawPath(shape);
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(base.darker(150), step / 2.));
		p.drawPath(shape);
	} break;
	}
	p.restore();
	return true;
}

void PaintBubble(QPainter &p, const SimpleBubble &args) {
	if (PaintStyledBubble(p, args)) {
		return;
	}
	if (UsePatternBubble(args)) {
		PaintPatternBubble(p, args);
	} else {
		PaintSolidBubble(p, args);
	}
}

void PaintBubble(QPainter &p, const ComplexBubble &args) {
	if (args.selection.empty()) {
		PaintBubble(p, args.simple);
		return;
	}
	const auto rect = args.simple.geometry;
	const auto left = rect.x();
	const auto width = rect.width();
	const auto top = rect.y();
	const auto bottom = top + rect.height();
	auto from = top;
	for (const auto &selected : args.selection) {
		if (selected.top > from) {
			PaintBubblePiece(
				p,
				args.simple,
				QRect(left, from, width, selected.top - from),
				false,
				(from <= top),
				false);
		}
		PaintBubblePiece(
			p,
			args.simple,
			QRect(left, selected.top, width, selected.height),
			true,
			(selected.top <= top),
			(selected.top + selected.height >= bottom));
		from = selected.top + selected.height;
	}
	if (from < bottom) {
		PaintBubblePiece(
			p,
			args.simple,
			QRect(left, from, width, bottom - from),
			false,
			false,
			true);
	}
}

void PaintBubble(QPainter &p, const BubbleWithGaps &args) {
	if (args.gaps.empty()) {
		PaintBubble(p, args.simple);
		return;
	}
	const auto rect = args.simple.geometry;
	const auto left = rect.x();
	const auto width = rect.width();
	const auto top = rect.y();
	const auto bottom = top + rect.height();
	const auto pattern = UsePatternBubble(args.simple);
	const auto paintStrip = [&](
			const QImage &mask,
			QImage &patternCache,
			QImage &solidCache,
			QColor &solidColor,
			int y) {
		const auto target = QRect(
			left,
			y,
			width,
			mask.height() / int(mask.devicePixelRatio()));
		if (pattern) {
			const auto wasOpacity = p.opacity();
			p.setOpacity(PatternBubbleOpacity(args.simple, wasOpacity));
			PaintPatternBubblePart(
				p,
				args.simple.patternViewport,
				args.simple.pattern->pixmap,
				target,
				mask,
				patternCache);
			p.setOpacity(wasOpacity);
		} else {
			const auto &st = args.simple.st->messageStyle(
				args.simple.outbg,
				args.simple.selected);
			const auto color = st.msgBg->c;
			if (solidCache.size() != mask.size() || solidColor != color) {
				if (solidCache.size() != mask.size()) {
					solidCache = QImage(
						mask.size(),
						QImage::Format_ARGB32_Premultiplied);
					solidCache.setDevicePixelRatio(mask.devicePixelRatio());
				}
				style::colorizeImage(mask, color, &solidCache);
				solidColor = color;
			}
			p.drawImage(target, solidCache);
		}
	};
	const auto torn = args.torn;
	const auto stripHeight = torn->maskTop.height()
		/ int(torn->maskTop.devicePixelRatio());
	auto from = top;
	for (const auto &gap : args.gaps) {
		const auto gapTop = gap.top;
		const auto gapBottom = gap.top + gap.height;
		if (gapTop > from) {
			PaintBubblePiece(
				p,
				args.simple,
				QRect(left, from, width, gapTop - from),
				(from <= top),
				false);
			paintStrip(
				torn->maskBottom,
				torn->patternCacheBottom,
				torn->solidCacheBottom,
				torn->solidColorBottom,
				gapTop);
		}
		if (gapBottom < bottom) {
			paintStrip(
				torn->maskTop,
				torn->patternCacheTop,
				torn->solidCacheTop,
				torn->solidColorTop,
				gapBottom - stripHeight);
		}
		from = gapBottom;
	}
	if (from < bottom) {
		PaintBubblePiece(
			p,
			args.simple,
			QRect(left, from, width, bottom - from),
			false,
			true);
	}
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target) {
	const auto factor = pixmap.devicePixelRatio();
	if (viewport.size() * factor == pixmap.size()) {
		const auto fill = target.intersected(viewport);
		if (fill.isEmpty()) {
			return;
		}
		p.drawPixmap(fill, pixmap, QRect(
			(fill.topLeft() - viewport.topLeft()) * factor,
			fill.size() * factor));
	} else {
		const auto to = viewport;
		const auto from = QRect(QPoint(), pixmap.size());
		const auto deviceRect = QRect(
			QPoint(),
			QSize(p.device()->width(), p.device()->height()));
		const auto clip = (target != deviceRect);
		if (clip) {
			p.setClipRect(target);
		}
		p.drawPixmap(to, pixmap, from);
		if (clip) {
			p.setClipping(false);
		}
	}
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target,
		const QImage &mask,
		QImage &cache) {
	if (mask.isNull()) {
		// Tristegram: nothing to paint through, avoid the assertion crash.
		return;
	}
	Expects(mask.bytesPerLine() == mask.width() * 4);
	Expects(mask.format() == QImage::Format_ARGB32_Premultiplied);

	if (cache.size() != mask.size()) {
		cache = QImage(
			mask.size(),
			QImage::Format_ARGB32_Premultiplied);
	}
	cache.setDevicePixelRatio(mask.devicePixelRatio());
	Assert(cache.bytesPerLine() == cache.width() * 4);
	memcpy(cache.bits(), mask.constBits(), mask.sizeInBytes());

	auto q = QPainter(&cache);
	q.setCompositionMode(QPainter::CompositionMode_SourceIn);
	PaintPatternBubblePart(
		q,
		viewport.translated(-target.topLeft()),
		pixmap,
		QRect(QPoint(), cache.size() / int(cache.devicePixelRatio())));
	q.end();

	p.drawImage(target, cache);
}

void PaintPatternBubblePart(
		QPainter &p,
		const QRect &viewport,
		const QPixmap &pixmap,
		const QRect &target,
		Fn<void(QPainter&)> paintContent,
		QImage &cache) {
	Expects(paintContent != nullptr);

	const auto targetOrigin = target.topLeft();
	const auto targetSize = target.size();
	if (cache.size() != targetSize * style::DevicePixelRatio()) {
		cache = QImage(
			target.size() * style::DevicePixelRatio(),
			QImage::Format_ARGB32_Premultiplied);
		cache.setDevicePixelRatio(style::DevicePixelRatio());
	}
	cache.fill(Qt::transparent);
	auto q = QPainter(&cache);
	q.translate(-targetOrigin);
	paintContent(q);
	q.translate(targetOrigin);
	q.setCompositionMode(QPainter::CompositionMode_SourceIn);
	PaintPatternBubblePart(
		q,
		viewport.translated(-targetOrigin),
		pixmap,
		QRect(QPoint(), targetSize));
	q.end();

	p.drawImage(target, cache);
}

} // namespace Ui
