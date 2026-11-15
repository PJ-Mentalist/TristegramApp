/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_recorder.h"

#include "base/timer.h"
#include "core/file_utilities.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "lang/lang_keys.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_basic.h"

#include <crl/crl_object_on_queue.h>

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QPointer>
#include <QtCore/QStandardPaths>
#include <QtGui/QCursor>

#include <atomic>

namespace Tristegram {
namespace {

using namespace FFmpeg;

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

// Encodes frames to H.264 in an mp4 file, lives on its own queue.
class Encoder final {
public:
	explicit Encoder(crl::weak_on_queue<Encoder> weak);
	~Encoder();

	void setup(QString path, int fps);

	void push(QImage frame, crl::time timestamp);
	void finish();

private:
	bool init(QSize size);
	bool write(const FramePointer &frame);
	void close();

#if DA_FFMPEG_CONST_WRITE_CALLBACK
	static int Write(void *opaque, const uint8_t *buffer, int size);
#else
	static int Write(void *opaque, uint8_t *buffer, int size);
#endif
	static int64_t Seek(void *opaque, int64_t offset, int whence);

	QFile _file;
	int _fps = 24;
	FormatPointer _format;
	CodecPointer _codec;
	AVStream *_stream = nullptr;
	FramePointer _frame;
	SwscalePointer _sws;
	QSize _size;
	crl::time _first = -1;
	int64 _lastPts = -1;
	bool _failed = false;
	bool _finished = false;

};

Encoder::Encoder(crl::weak_on_queue<Encoder>) {
}

void Encoder::setup(QString path, int fps) {
	_file.setFileName(path);
	_fps = fps;
}

Encoder::~Encoder() {
	finish();
}

#if DA_FFMPEG_CONST_WRITE_CALLBACK
int Encoder::Write(void *opaque, const uint8_t *buffer, int size) {
#else
int Encoder::Write(void *opaque, uint8_t *buffer, int size) {
#endif
	const auto that = static_cast<Encoder*>(opaque);
	const auto written = that->_file.write(
		reinterpret_cast<const char*>(buffer),
		size);
	return (written == size) ? size : AVERROR(EIO);
}

int64_t Encoder::Seek(void *opaque, int64_t offset, int whence) {
	const auto that = static_cast<Encoder*>(opaque);
	auto &file = that->_file;
	switch (whence) {
	case SEEK_SET: return file.seek(offset) ? offset : -1;
	case SEEK_CUR: return file.seek(file.pos() + offset) ? file.pos() : -1;
	case SEEK_END: return file.seek(file.size() + offset) ? file.pos() : -1;
	case AVSEEK_SIZE: return file.size();
	}
	return -1;
}

bool Encoder::init(QSize size) {
	_size = size;
	if (!_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		LogError(u"ScreenRecorder"_q, u"Could not open "_q + _file.fileName());
		return false;
	}
	_format = MakeWriteFormatPointer(
		static_cast<void*>(this),
		nullptr,
		&Encoder::Write,
		&Encoder::Seek,
		"mp4"_q);
	if (!_format) {
		return false;
	}
	auto codec = avcodec_find_encoder_by_name("libopenh264");
	if (!codec) {
		codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!codec) {
		LogError(u"avcodec_find_encoder"_q, u"H264"_q);
		return false;
	}
	_stream = avformat_new_stream(_format.get(), codec);
	_codec = CodecPointer(avcodec_alloc_context3(codec));
	if (!_stream || !_codec) {
		return false;
	}
	_codec->codec_id = codec->id;
	_codec->codec_type = AVMEDIA_TYPE_VIDEO;
	_codec->width = size.width();
	_codec->height = size.height();
	_codec->time_base = AVRational{ 1, 1000 };
	_codec->framerate = AVRational{ _fps, 1 };
	_codec->pix_fmt = AV_PIX_FMT_YUV420P;
	_codec->gop_size = _fps * 2;
	_codec->max_b_frames = 0;
	// Interface frames compress well, keep the size small.
	_codec->bit_rate = std::clamp(
		int64(size.width()) * size.height() * _fps / 6,
		int64(600'000),
		int64(6'000'000));
	if (_format->oformat->flags & AVFMT_GLOBALHEADER) {
		_codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	auto error = AvErrorWrap(avcodec_open2(_codec.get(), codec, nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error);
		return false;
	}
	error = AvErrorWrap(avcodec_parameters_from_context(
		_stream->codecpar,
		_codec.get()));
	if (error) {
		LogError(u"avcodec_parameters_from_context"_q, error);
		return false;
	}
	_stream->time_base = _codec->time_base;
	_frame = MakeFramePointer();
	if (!_frame) {
		return false;
	}
	_frame->format = _codec->pix_fmt;
	_frame->width = size.width();
	_frame->height = size.height();
	error = AvErrorWrap(av_frame_get_buffer(_frame.get(), 0));
	if (error) {
		LogError(u"av_frame_get_buffer"_q, error);
		return false;
	}
	error = AvErrorWrap(avformat_write_header(_format.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return false;
	}
	return true;
}

void Encoder::push(QImage frame, crl::time timestamp) {
	if (_failed || _finished || frame.isNull()) {
		return;
	}
	if (_size.isEmpty()) {
		const auto size = QSize(
			frame.width() & ~1,
			frame.height() & ~1);
		if (size.isEmpty() || !init(size)) {
			_failed = true;
			close();
			return;
		}
	}
	if (frame.size() != _size) {
		frame = frame.copy(QRect(QPoint(), _size));
	}
	if (_first < 0) {
		_first = timestamp;
	}
	const auto pts = int64(timestamp - _first);
	if (pts <= _lastPts) {
		return;
	}
	_lastPts = pts;
	_sws = MakeSwscalePointer(
		_size,
		AV_PIX_FMT_BGRA,
		_size,
		AV_PIX_FMT_YUV420P,
		&_sws);
	if (!_sws) {
		_failed = true;
		return;
	}
	const auto error = AvErrorWrap(av_frame_make_writable(_frame.get()));
	if (error) {
		_failed = true;
		return;
	}
	const uint8_t *source[1] = { frame.constBits() };
	int stride[1] = { int(frame.bytesPerLine()) };
	sws_scale(
		_sws.get(),
		source,
		stride,
		0,
		_size.height(),
		_frame->data,
		_frame->linesize);
	_frame->pts = pts;
	if (!write(_frame)) {
		_failed = true;
	}
}

bool Encoder::write(const FramePointer &frame) {
	auto error = AvErrorWrap(avcodec_send_frame(_codec.get(), frame.get()));
	if (error) {
		LogError(u"avcodec_send_frame"_q, error);
		return false;
	}
	auto packet = av_packet_alloc();
	const auto guard = gsl::finally([&] { av_packet_free(&packet); });
	while (true) {
		error = AvErrorWrap(avcodec_receive_packet(_codec.get(), packet));
		if (error.code() == AVERROR(EAGAIN) || error.code() == AVERROR_EOF) {
			return true;
		} else if (error) {
			LogError(u"avcodec_receive_packet"_q, error);
			return false;
		}
		packet->stream_index = _stream->index;
		av_packet_rescale_ts(packet, _codec->time_base, _stream->time_base);
		error = AvErrorWrap(av_interleaved_write_frame(_format.get(), packet));
		if (error) {
			LogError(u"av_interleaved_write_frame"_q, error);
			return false;
		}
	}
}

void Encoder::finish() {
	if (_finished) {
		return;
	}
	_finished = true;
	if (_format && _codec && !_failed) {
		if (write(nullptr)) {
			const auto error = AvErrorWrap(av_write_trailer(_format.get()));
			if (error) {
				LogError(u"av_write_trailer"_q, error);
			}
		}
	}
	close();
}

void Encoder::close() {
	_sws = nullptr;
	_frame = nullptr;
	_codec = nullptr;
	_format = nullptr;
	if (_file.isOpen()) {
		_file.close();
	}
}

[[nodiscard]] QString RecordingsFolder() {
	auto base = QStandardPaths::writableLocation(
		QStandardPaths::MoviesLocation);
	if (base.isEmpty()) {
		base = QStandardPaths::writableLocation(
			QStandardPaths::DownloadLocation);
	}
	const auto folder = base + u"/Tristegram"_q;
	QDir().mkpath(folder);
	return folder;
}

// One recording at a time for the whole app.
class Recorder final {
public:
	static Recorder &Instance() {
		static const auto result = new Recorder();
		return *result;
	}

	[[nodiscard]] rpl::producer<bool> recordingValue() const {
		return _recording.value();
	}
	[[nodiscard]] bool recording() const {
		return _recording.current();
	}
	[[nodiscard]] crl::time started() const {
		return _started;
	}

	void start(not_null<QWidget*> window) {
		if (recording()) {
			return;
		}
		_window = window.get();
		const auto fps = std::clamp(
			Extera::Int(u"tristegram_record_fps"_q),
			10,
			60);
		const auto stamp = QDateTime::currentDateTime().toString(
			u"yyyy-MM-dd_HH-mm-ss"_q);
		_path = RecordingsFolder() + u"/Tristegram_"_q + stamp + u".mp4"_q;
		_encoder = std::make_unique<crl::object_on_queue<Encoder>>();
		_encoder->with([path = _path, fps](Encoder &encoder) {
			encoder.setup(path, fps);
		});
		_pending = std::make_shared<std::atomic<int>>(0);
		_started = crl::now();
		_recording = true;
		_timer.callEach(1000 / fps);
		capture();
	}

	QString stop() {
		if (!recording()) {
			return QString();
		}
		_timer.cancel();
		_recording = false;
		if (_encoder) {
			_encoder->with([](Encoder &encoder) {
				encoder.finish();
			});
			_encoder = nullptr;
		}
		return _path;
	}

private:
	Recorder() : _timer([=] { capture(); }) {
	}

	void capture() {
		const auto window = _window.data();
		if (!window || !_encoder) {
			stop();
			return;
		}
		if (!window->isVisible() || window->isMinimized()) {
			return;
		}
		// Weak computers: skip frames instead of piling them up.
		if (_pending->load() > 2) {
			return;
		}
		const auto scale = std::clamp(
			Extera::Int(u"tristegram_record_scale"_q),
			25,
			100) / 100.;
		const auto full = window->size();
		const auto size = QSize(
			std::max(int(full.width() * scale) & ~1, 2),
			std::max(int(full.height() * scale) & ~1, 2));
		auto frame = QImage(size, QImage::Format_ARGB32_Premultiplied);
		frame.fill(Qt::black);
		{
			auto p = QPainter(&frame);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			p.scale(
				size.width() / double(full.width()),
				size.height() / double(full.height()));
			window->render(
				&p,
				QPoint(),
				QRegion(),
				QWidget::DrawChildren);
			// The cursor, so it is clear what happens on the video.
			const auto cursor = window->mapFromGlobal(QCursor::pos());
			if (window->rect().contains(cursor)) {
				const auto colors = CurrentLogoStyle();
				auto hq = PainterHighQualityEnabler(p);
				auto glow = colors.from;
				glow.setAlphaF(0.35);
				p.setPen(Qt::NoPen);
				p.setBrush(glow);
				p.drawEllipse(QPointF(cursor), Px(14), Px(14));
				p.setBrush(QColor(255, 255, 255));
				p.drawEllipse(QPointF(cursor), Px(4), Px(4));
			}
		}
		const auto pending = _pending;
		++*pending;
		const auto now = crl::now();
		_encoder->with([=, frame = std::move(frame)](Encoder &encoder) {
			encoder.push(frame, now);
			--*pending;
		});
	}

	QPointer<QWidget> _window;
	QString _path;
	std::unique_ptr<crl::object_on_queue<Encoder>> _encoder;
	std::shared_ptr<std::atomic<int>> _pending;
	crl::time _started = 0;
	rpl::variable<bool> _recording = false;
	base::Timer _timer;

};

class RecordButton final : public Ui::AbstractButton {
public:
	RecordButton(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void toggle();

	const not_null<Window::SessionController*> _controller;
	base::Timer _tick;

};

RecordButton::RecordButton(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: AbstractButton(parent)
, _controller(controller)
, _tick([=] { update(); }) {
	setPointerCursor(true);
	resize(Px(40), Px(40));
	setClickedCallback([=] { toggle(); });
	Recorder::Instance().recordingValue(
	) | rpl::on_next([=](bool recording) {
		resize(recording ? Px(92) : Px(40), Px(40));
		if (recording) {
			_tick.callEach(250);
		} else {
			_tick.cancel();
		}
		update();
	}, lifetime());
}

void RecordButton::toggle() {
	auto &recorder = Recorder::Instance();
	if (recorder.recording()) {
		const auto path = recorder.stop();
		if (!path.isEmpty()) {
			_controller->showToast(Ui::Toast::Config{
				.text = { tr::lng_tgr_saved(tr::now) + u"\n"_q + path },
				.duration = crl::time(4000),
			});
			File::ShowInFolder(path);
		}
	} else {
		recorder.start(_controller->widget());
		_controller->showToast(tr::lng_tgr_started(tr::now));
	}
}

void RecordButton::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto recording = Recorder::Instance().recording();
	const auto over = isOver();
	const auto r = QRectF(rect()).marginsRemoved(QMarginsF(
		Px(4),
		Px(6),
		Px(4),
		Px(6)));
	const auto radius = r.height() / 2.;
	if (recording) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0xFF, 0x3B, 0x30, over ? 70 : 46));
		p.drawRoundedRect(r, radius, radius);
		const auto pulse = 0.6
			+ 0.4 * std::abs(std::sin(crl::now() / 420.));
		const auto dot = QPointF(r.x() + radius, r.center().y());
		p.setBrush(QColor(0xFF, 0x3B, 0x30, int(255 * pulse)));
		p.drawEllipse(dot, Px(5), Px(5));
		const auto seconds = int((crl::now() - Recorder::Instance().started()) / 1000);
		p.setPen(QColor(0xFF, 0x3B, 0x30));
		p.setFont(st::semiboldFont);
		p.drawText(
			QRectF(dot.x() + Px(9), r.y(), r.right() - dot.x() - Px(9), r.height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			QString::number(seconds / 60).rightJustified(2, '0')
				+ ':'
				+ QString::number(seconds % 60).rightJustified(2, '0'));
	} else {
		if (over) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawEllipse(QRectF(rect()).marginsRemoved(QMarginsF(Px(4), Px(4), Px(4), Px(4))));
		}
		const auto c = QRectF(rect()).center();
		p.setPen(QPen(st::menuIconFg->c, Px(2)));
		p.setBrush(Qt::NoBrush);
		p.drawEllipse(c, Px(9), Px(9));
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(0xFF, 0x3B, 0x30));
		p.drawEllipse(c, Px(9) / 2., Px(9) / 2.);
	}
}

} // namespace

object_ptr<Ui::RpWidget> CreateRecordButton(
		QWidget *parent,
		not_null<Window::SessionController*> controller) {
	return object_ptr<RecordButton>(parent, controller);
}

void StopScreenRecording() {
	Recorder::Instance().stop();
}

} // namespace Tristegram
