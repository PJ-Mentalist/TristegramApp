/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_ai_writer.h"

#include "core/file_utilities.h"
#include "extera/extera_config.h"
#include "extera/tristegram_logo.h"
#include "lang/lang_keys.h"
#include "ui/effects/animations.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtWidgets/QMenu>
#include <QtWidgets/QTextEdit>

namespace Tristegram {
namespace {

constexpr auto kMaxInput = 8000;
constexpr auto kRecentLimit = 6;

enum class Action {
	Fix,
	Improve,
	Shorter,
	Longer,
	Polite,
	Friendly,
	Formal,
	Funny,
	Confident,
	Emoji,
	Translate,
	Custom,
};

struct Job {
	Action action = Action::Fix;
	QString extra; // a language or the user's own request
};

// The part of the field the job works on, to put the answer back.
struct Target {
	int from = 0;
	int till = 0;
	QString text;
	bool whole = false;
};

struct Models {
	QString id;
	QString title;
};

[[nodiscard]] const std::vector<Models> &KnownModels() {
	static const auto result = std::vector<Models>{
		{ u"gemini-2.5-flash"_q, u"Gemini 2.5 Flash"_q },
		{ u"gemini-2.5-flash-lite"_q, u"Gemini 2.5 Flash-Lite"_q },
		{ u"gemini-2.5-pro"_q, u"Gemini 2.5 Pro"_q },
	};
	return result;
}

[[nodiscard]] QString Key() {
	return Extera::String(u"ai_chat_key"_q).trimmed();
}

[[nodiscard]] QString Model() {
	const auto model = Extera::String(u"tristegram_ai_model"_q).trimmed();
	return model.isEmpty() ? u"gemini-2.5-flash"_q : model;
}

[[nodiscard]] QNetworkAccessManager *Network() {
	static const auto result = new QNetworkAccessManager(qApp);
	return result;
}

[[nodiscard]] int Px(int value) {
	return style::ConvertScale(value);
}

// A light that runs over the field while Gemini writes.
class Shimmer final : public Ui::RpWidget {
public:
	explicit Shimmer(not_null<Ui::InputField*> field)
	: RpWidget(field)
	, _animation([=] { update(); }) {
		setAttribute(Qt::WA_TransparentForMouseEvents);
		field->sizeValue(
		) | rpl::on_next([=](QSize size) {
			setGeometry(QRect(QPoint(), size));
		}, lifetime());
		_animation.start();
		show();
		raise();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		auto hq = PainterHighQualityEnabler(p);
		const auto colors = CurrentLogoStyle();
		const auto r = QRectF(rect());
		const auto period = 1300.;
		const auto t = std::fmod(double(crl::now()), period) / period;
		const auto band = r.width() * 0.45;
		const auto x = -band + (r.width() + band * 2) * t;

		auto from = colors.from;
		auto to = colors.to;
		from.setAlphaF(0.20);
		to.setAlphaF(0.20);
		auto clear = colors.from;
		clear.setAlphaF(0.);
		auto sweep = QLinearGradient(x - band, 0., x + band, 0.);
		sweep.setColorAt(0., clear);
		sweep.setColorAt(0.4, from);
		sweep.setColorAt(0.6, to);
		sweep.setColorAt(1., clear);
		p.setPen(Qt::NoPen);
		p.setBrush(sweep);
		p.drawRoundedRect(r, Px(10), Px(10));

		// A thin line of the logo colors along the bottom.
		auto line = QLinearGradient(r.topLeft(), r.topRight());
		auto a = colors.from;
		auto b = colors.to;
		a.setAlphaF(0.9);
		b.setAlphaF(0.9);
		line.setColorAt(0., clear);
		line.setColorAt(std::clamp(t - 0.25, 0., 1.), clear);
		line.setColorAt(std::clamp(t, 0.001, 0.999), a);
		line.setColorAt(std::clamp(t + 0.25, 0., 1.), b);
		line.setColorAt(1., clear);
		p.setBrush(line);
		p.drawRoundedRect(
			QRectF(r.x() + Px(6), r.bottom() - Px(2), r.width() - Px(12), Px(2)),
			Px(1),
			Px(1));
	}

private:
	Ui::Animations::Basic _animation;

};

struct Running {
	QPointer<QNetworkReply> reply;
	QPointer<Ui::InputField> field;
	QPointer<Shimmer> shimmer;
};

[[nodiscard]] Running &Current() {
	static auto result = Running();
	return result;
}

[[nodiscard]] bool IsRunning() {
	return Current().reply != nullptr;
}

void StopRunning() {
	auto &current = Current();
	if (const auto shimmer = current.shimmer.data()) {
		delete shimmer;
	}
	if (const auto field = current.field.data()) {
		field->rawTextEdit()->setReadOnly(false);
	}
	if (const auto reply = current.reply.data()) {
		current.reply = nullptr;
		reply->abort();
	}
	current = Running();
}

[[nodiscard]] QString LanguageName(const QString &code) {
	if (code == u"en"_q) return u"English"_q;
	if (code == u"ru"_q) return u"Russian"_q;
	if (code == u"uk"_q) return u"Ukrainian"_q;
	if (code == u"de"_q) return u"German"_q;
	if (code == u"es"_q) return u"Spanish"_q;
	if (code == u"fr"_q) return u"French"_q;
	if (code == u"ja"_q) return u"Japanese"_q;
	if (code == u"pl"_q) return u"Polish"_q;
	return code;
}

[[nodiscard]] QString TaskText(const Job &job) {
	switch (job.action) {
	case Action::Fix:
		return u"Fix the spelling, grammar and punctuation mistakes. "
			"Change nothing else: keep the words, the slang, the tone, "
			"the letter case style and the emoji."_q;
	case Action::Improve:
		return u"Rewrite the message so that it reads clearly and "
			"naturally, fix all the mistakes, keep the tone and the "
			"meaning."_q;
	case Action::Shorter:
		return u"Make the message shorter and to the point, keep the "
			"meaning and the tone."_q;
	case Action::Longer:
		return u"Expand the message a little with natural detail, keep "
			"the meaning and the tone."_q;
	case Action::Polite:
		return u"Rewrite the message in a polite and respectful tone."_q;
	case Action::Friendly:
		return u"Rewrite the message in a warm, friendly, casual tone."_q;
	case Action::Formal:
		return u"Rewrite the message in a formal business tone."_q;
	case Action::Funny:
		return u"Rewrite the message with light, kind humor."_q;
	case Action::Confident:
		return u"Rewrite the message so that it sounds confident and "
			"clear, without being rude."_q;
	case Action::Emoji:
		return u"Add a few fitting emoji to the message. Do not change "
			"the words."_q;
	case Action::Translate:
		return u"Translate the message into "_q
			+ LanguageName(job.extra)
			+ u". Keep the tone, the slang level and the emoji."_q;
	case Action::Custom:
		return u"Do this with the message: "_q + job.extra;
	}
	return QString();
}

[[nodiscard]] QString SystemPrompt(const Job &job) {
	auto result = u"You are the writing assistant of a messenger. The "
		"user gives you a message they are about to send. "_q
		+ TaskText(job)
		+ u" "_q;
	if (job.action != Action::Translate) {
		result += u"Answer in the same language as the message. "_q;
	}
	result += u"Keep names, links, numbers, @mentions, #hashtags and "
		"line breaks. "_q;
	if (!Extera::Bool(u"tristegram_ai_keep_emoji"_q)
		&& job.action != Action::Emoji) {
		result += u"You may remove emoji if the text reads better. "_q;
	}
	const auto style = Extera::String(u"tristegram_ai_style"_q).trimmed();
	if (!style.isEmpty() && job.action != Action::Fix) {
		result += u"The user's own writing style, follow it: "_q
			+ style
			+ u" "_q;
	}
	result += u"Reply with the resulting message only: no quotes, no "
		"comments, no explanations, no markdown."_q;
	return result;
}

[[nodiscard]] double Temperature(const Job &job, bool again) {
	const auto base = std::clamp(
		Extera::Int(u"tristegram_ai_temperature"_q),
		0,
		10) / 10.;
	if (job.action == Action::Fix || job.action == Action::Translate) {
		return again ? 0.5 : std::min(base, 0.2);
	}
	return again ? std::min(base + 0.35, 1.3) : base;
}

[[nodiscard]] QString ErrorFrom(const QByteArray &data) {
	const auto object = QJsonDocument::fromJson(data).object();
	const auto message = object.value(u"error"_q).toObject()
		.value(u"message"_q).toString();
	if (!message.isEmpty()) {
		return message;
	}
	const auto blocked = object.value(u"promptFeedback"_q).toObject()
		.value(u"blockReason"_q).toString();
	if (!blocked.isEmpty()) {
		return u"Blocked: "_q + blocked;
	}
	return QString();
}

// Gemini puts quotes around the answer sometimes.
[[nodiscard]] QString CleanAnswer(QString text, const QString &original) {
	text = text.trimmed();
	const auto quoted = [&](QChar open, QChar close) {
		return text.size() > 1
			&& text.startsWith(open)
			&& text.endsWith(close)
			&& !(original.trimmed().startsWith(open));
	};
	if (quoted('"', '"')
		|| quoted(QChar(0x00AB), QChar(0x00BB))
		|| quoted(QChar(0x201C), QChar(0x201D))) {
		text = text.mid(1, text.size() - 2).trimmed();
	}
	if (text.startsWith(u"```"_q) && text.endsWith(u"```"_q)) {
		text = text.mid(3, text.size() - 6);
		const auto line = text.indexOf('\n');
		if (line >= 0 && line < 16) {
			text = text.mid(line + 1);
		}
		text = text.trimmed();
	}
	return text;
}

void Request(
		const Job &job,
		const QString &text,
		bool again,
		Fn<void(QString)> done,
		Fn<void(QString)> fail) {
	auto request = QNetworkRequest(QUrl(
		u"https://generativelanguage.googleapis.com/v1beta/models/"_q
		+ Model()
		+ u":generateContent"_q));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader("x-goog-api-key", Key().toUtf8());
	auto generation = QJsonObject{
		{ u"temperature"_q, Temperature(job, again) },
	};
	if (Model().startsWith(u"gemini-2.5-flash"_q)) {
		// Rewriting a message needs no long thinking: faster answers.
		generation.insert(u"thinkingConfig"_q, QJsonObject{
			{ u"thinkingBudget"_q, 0 },
		});
	}
	const auto body = QJsonDocument(QJsonObject{
		{ u"systemInstruction"_q, QJsonObject{
			{ u"parts"_q, QJsonArray{ QJsonObject{
				{ u"text"_q, SystemPrompt(job) },
			} } },
		} },
		{ u"contents"_q, QJsonArray{ QJsonObject{
			{ u"role"_q, u"user"_q },
			{ u"parts"_q, QJsonArray{ QJsonObject{
				{ u"text"_q, text.left(kMaxInput) },
			} } },
		} } },
		{ u"generationConfig"_q, generation },
	}).toJson(QJsonDocument::Compact);
	const auto reply = Network()->post(request, body);
	Current().reply = reply;
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		if (Current().reply != reply) {
			return; // Cancelled.
		}
		Current().reply = nullptr;
		const auto data = reply->readAll();
		const auto object = QJsonDocument::fromJson(data).object();
		const auto candidates = object.value(u"candidates"_q).toArray();
		auto answer = QString();
		if (!candidates.isEmpty()) {
			const auto parts = candidates[0].toObject()
				.value(u"content"_q).toObject()
				.value(u"parts"_q).toArray();
			for (const auto &part : parts) {
				const auto entry = part.toObject();
				if (!entry.value(u"thought"_q).toBool()) {
					answer += entry.value(u"text"_q).toString();
				}
			}
		}
		answer = CleanAnswer(answer, text);
		if (!answer.isEmpty()) {
			done(answer);
		} else {
			const auto error = ErrorFrom(data);
			fail(error.isEmpty() ? reply->errorString() : error);
		}
	});
}

[[nodiscard]] Target CaptureTarget(not_null<Ui::InputField*> field) {
	const auto edit = field->rawTextEdit();
	const auto cursor = edit->textCursor();
	auto result = Target();
	result.from = cursor.selectionStart();
	result.till = cursor.selectionEnd();
	if (result.from == result.till) {
		auto end = QTextCursor(edit->document());
		end.movePosition(QTextCursor::End);
		result.from = 0;
		result.till = end.position();
		result.whole = true;
	}
	result.text = field->getTextWithTagsPart(result.from, result.till).text;
	return result;
}

// Puts the answer in place of the text the job started with. The field
// keeps the old text in its undo history, so Ctrl+Z brings it back.
bool ApplyAnswer(
		not_null<Ui::InputField*> field,
		const Target &target,
		const QString &answer) {
	if (target.whole) {
		auto end = QTextCursor(field->rawTextEdit()->document());
		end.movePosition(QTextCursor::End);
		if (end.position() != target.till
			|| field->getTextWithTagsPart(0, target.till).text
				!= target.text) {
			return false;
		}
		field->setTextWithTags(
			{ answer, {} },
			Ui::InputField::HistoryAction::NewEntry);
		auto cursor = field->rawTextEdit()->textCursor();
		cursor.movePosition(QTextCursor::End);
		field->rawTextEdit()->setTextCursor(cursor);
	} else {
		const auto now = field->getTextWithTagsPart(
			target.from,
			target.till).text;
		if (now != target.text) {
			return false;
		}
		const auto edit = field->rawTextEdit();
		auto cursor = edit->textCursor();
		cursor.beginEditBlock();
		cursor.setPosition(target.from);
		cursor.setPosition(target.till, QTextCursor::KeepAnchor);
		cursor.insertText(answer);
		cursor.endEditBlock();
		edit->setTextCursor(cursor);
	}
	field->setFocus();
	return true;
}

[[nodiscard]] QStringList RecentRequests() {
	auto result = QStringList();
	const auto list = QJsonDocument::fromJson(
		Extera::String(u"tristegram_ai_recent"_q).toUtf8()).array();
	for (const auto &entry : list) {
		const auto text = entry.toString().trimmed();
		if (!text.isEmpty()) {
			result.push_back(text);
		}
	}
	return result;
}

void RememberRequest(const QString &text) {
	auto list = RecentRequests();
	list.removeAll(text);
	list.push_front(text);
	while (list.size() > kRecentLimit) {
		list.pop_back();
	}
	auto array = QJsonArray();
	for (const auto &entry : list) {
		array.push_back(entry);
	}
	Extera::Config::Instance().setString(
		u"tristegram_ai_recent"_q,
		QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void Run(
	not_null<Window::SessionController*> controller,
	not_null<Ui::InputField*> field,
	Job job,
	bool again = false);

// The answer with the original above it: replace, try once more, copy.
void ShowPreview(
		not_null<Window::SessionController*> controller,
		QPointer<Ui::InputField> field,
		Job job,
		Target target,
		QString answer) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgai_preview_title());
		box->setWidth(st::boxWideWidth);
		const auto was = box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_tgai_preview_was(tr::now)
				+ u"\n"_q
				+ ((target.text.size() > 600)
					? (target.text.left(600) + QChar(0x2026))
					: target.text),
			st::boxDividerLabel));
		was->setSelectable(true);
		const auto result = box->addRow(
			object_ptr<Ui::InputField>(
				box,
				st::newGroupDescription,
				Ui::InputField::Mode::MultiLine,
				tr::lng_tgai_preview_placeholder(),
				answer),
			st::boxRowPadding + QMargins(0, Px(10), 0, 0));
		result->setMaxHeight(Px(260));
		box->setFocusCallback([=] {
			result->setFocusFast();
			result->setCursorPosition(result->getLastText().size());
		});
		const auto replace = [=] {
			const auto text = result->getLastText().trimmed();
			const auto strong = field.data();
			box->closeBox();
			if (!strong || text.isEmpty()) {
				return;
			}
			if (!ApplyAnswer(strong, target, text)) {
				controller->showToast(tr::lng_tgai_changed(tr::now));
			}
		};
		result->submits() | rpl::on_next(replace, result->lifetime());
		box->addButton(tr::lng_tgai_replace(), replace);
		box->addButton(tr::lng_tgai_again(), [=] {
			box->closeBox();
			if (const auto strong = field.data()) {
				Run(controller, strong, job, true);
			}
		});
		box->addLeftButton(tr::lng_tgai_copy(), [=] {
			QGuiApplication::clipboard()->setText(result->getLastText());
			box->showToast(tr::lng_tgai_copied(tr::now));
		});
	}));
}

void Run(
		not_null<Window::SessionController*> controller,
		not_null<Ui::InputField*> field,
		Job job,
		bool again) {
	if (Key().isEmpty()) {
		controller->showToast(tr::lng_tgai_need_key(tr::now));
		ShowAiWriterSettings(controller);
		return;
	}
	if (IsRunning()) {
		StopRunning();
	}
	const auto target = CaptureTarget(field);
	if (target.text.trimmed().isEmpty()) {
		controller->showToast(tr::lng_tgai_empty(tr::now));
		return;
	}
	auto &current = Current();
	current.field = field.get();
	current.shimmer = Ui::CreateChild<Shimmer>(field.get());
	field->rawTextEdit()->setReadOnly(true);

	const auto weak = QPointer<Ui::InputField>(field.get());
	const auto finish = [=] {
		if (const auto shimmer = Current().shimmer.data()) {
			delete shimmer;
		}
		if (const auto strong = weak.data()) {
			strong->rawTextEdit()->setReadOnly(false);
		}
		Current() = Running();
	};
	Request(job, target.text, again, crl::guard(controller, [=](
			QString answer) {
		finish();
		const auto strong = weak.data();
		if (!strong) {
			return;
		}
		if (answer == target.text) {
			controller->showToast(tr::lng_tgai_nothing(tr::now));
			return;
		}
		if (Extera::Bool(u"tristegram_ai_preview"_q)) {
			ShowPreview(controller, weak, job, target, answer);
		} else if (ApplyAnswer(strong, target, answer)) {
			controller->showToast(tr::lng_tgai_done(tr::now));
		} else {
			controller->showToast(tr::lng_tgai_changed(tr::now));
		}
	}), crl::guard(controller, [=](QString error) {
		finish();
		controller->showToast(tr::lng_tgai_error(tr::now) + u"\n"_q + error);
	}));
}

// "Your own request": a field with the last requests as quick buttons.
void AskCustom(
		not_null<Window::SessionController*> controller,
		QPointer<Ui::InputField> field) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgai_custom_title());
		const auto input = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			tr::lng_tgai_custom_placeholder()));
		input->setMaxLength(500);
		const auto recent = RecentRequests();
		if (!recent.isEmpty()) {
			Ui::AddSubsectionTitle(
				box->verticalLayout(),
				tr::lng_tgai_custom_recent());
			for (const auto &text : recent) {
				const auto button = box->addRow(
					object_ptr<Ui::LinkButton>(
						box,
						(text.size() > 60)
							? (text.left(59) + QChar(0x2026))
							: text),
					st::boxRowPadding + QMargins(0, Px(2), 0, Px(4)));
				button->setClickedCallback([=] {
					input->setText(text);
					input->setFocus();
					input->setCursorPosition(text.size());
				});
			}
		}
		box->setFocusCallback([=] { input->setFocusFast(); });
		const auto go = [=] {
			const auto text = input->getLastText().trimmed();
			if (text.isEmpty()) {
				input->showError();
				return;
			}
			RememberRequest(text);
			box->closeBox();
			if (const auto strong = field.data()) {
				Run(controller, strong, { Action::Custom, text });
			}
		};
		input->submits() | rpl::on_next(go, input->lifetime());
		box->addButton(tr::lng_tgai_custom_go(), go);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

} // namespace

void FillAiWriterMenu(
		not_null<QMenu*> menu,
		not_null<Ui::InputField*> field,
		not_null<Window::SessionController*> controller) {
	if (!Extera::Bool(u"tristegram_ai_writer"_q)) {
		return;
	}
	const auto weak = QPointer<Ui::InputField>(field.get());
	const auto hasText = !field->getLastText().trimmed().isEmpty();
	menu->addSeparator();
	const auto sub = menu->addMenu(tr::lng_tgai_menu(tr::now));
	if (IsRunning()) {
		sub->addAction(tr::lng_tgai_cancel(tr::now), [] {
			StopRunning();
		});
		return;
	}
	const auto run = [=](Job job) {
		return [=] {
			if (const auto strong = weak.data()) {
				Run(controller, strong, job);
			}
		};
	};
	const auto add = [&](QMenu *to, const QString &text, Job job) {
		const auto action = to->addAction(text, run(job));
		action->setEnabled(hasText);
	};
	const auto selected = field->rawTextEdit()->textCursor().hasSelection();
	if (selected) {
		const auto hint = sub->addAction(tr::lng_tgai_selection(tr::now));
		hint->setEnabled(false);
	}
	add(sub, tr::lng_tgai_fix(tr::now), { Action::Fix });
	add(sub, tr::lng_tgai_improve(tr::now), { Action::Improve });
	add(sub, tr::lng_tgai_shorter(tr::now), { Action::Shorter });
	add(sub, tr::lng_tgai_longer(tr::now), { Action::Longer });

	const auto tone = sub->addMenu(tr::lng_tgai_tone(tr::now));
	add(tone, tr::lng_tgai_tone_polite(tr::now), { Action::Polite });
	add(tone, tr::lng_tgai_tone_friendly(tr::now), { Action::Friendly });
	add(tone, tr::lng_tgai_tone_formal(tr::now), { Action::Formal });
	add(tone, tr::lng_tgai_tone_funny(tr::now), { Action::Funny });
	add(tone, tr::lng_tgai_tone_confident(tr::now), { Action::Confident });

	const auto translate = sub->addMenu(tr::lng_tgai_translate(tr::now));
	const auto languages = std::vector<std::pair<QString, QString>>{
		{ u"en"_q, u"\U0001F1EC\U0001F1E7 English"_q },
		{ u"ru"_q, u"\U0001F1F7\U0001F1FA Русский"_q },
		{ u"uk"_q, u"\U0001F1FA\U0001F1E6 Українська"_q },
		{ u"de"_q, u"\U0001F1E9\U0001F1EA Deutsch"_q },
		{ u"es"_q, u"\U0001F1EA\U0001F1F8 Español"_q },
		{ u"fr"_q, u"\U0001F1EB\U0001F1F7 Français"_q },
		{ u"pl"_q, u"\U0001F1F5\U0001F1F1 Polski"_q },
		{ u"ja"_q, u"\U0001F1EF\U0001F1F5 日本語"_q },
	};
	for (const auto &[code, title] : languages) {
		add(translate, title, { Action::Translate, code });
	}
	add(sub, tr::lng_tgai_emoji(tr::now), { Action::Emoji });
	const auto custom = sub->addAction(tr::lng_tgai_custom(tr::now), [=] {
		AskCustom(controller, weak);
	});
	custom->setEnabled(hasText);
	sub->addSeparator();
	sub->addAction(tr::lng_tgai_settings_menu(tr::now), [=] {
		ShowAiWriterSettings(controller);
	});
	translate->setEnabled(hasText);
	tone->setEnabled(hasText);
}

void ShowAiWriterSettings(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_tgai_settings());
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();

		Ui::AddSubsectionTitle(content, tr::lng_tgai_key_title());
		const auto key = box->addRow(object_ptr<Ui::PasswordInput>(
			box,
			st::defaultInputField,
			tr::lng_tgai_key_placeholder(),
			Key()));
		const auto get = box->addRow(
			object_ptr<Ui::LinkButton>(box, tr::lng_tgai_key_get(tr::now)),
			st::boxRowPadding + QMargins(0, Px(8), 0, Px(4)));
		get->setClickedCallback([] {
			File::OpenUrl(u"https://aistudio.google.com/apikey"_q);
		});

		Ui::AddSubsectionTitle(content, tr::lng_tgai_model_title());
		const auto models = KnownModels();
		const auto current = Model();
		auto index = int(models.size());
		for (auto i = 0; i != int(models.size()); ++i) {
			if (models[i].id == current) {
				index = i;
			}
		}
		const auto group = std::make_shared<Ui::RadiobuttonGroup>(index);
		for (auto i = 0; i != int(models.size()); ++i) {
			box->addRow(
				object_ptr<Ui::Radiobutton>(
					box,
					group,
					i,
					models[i].title),
				st::boxRowPadding + QMargins(0, Px(4), 0, Px(4)));
		}
		box->addRow(
			object_ptr<Ui::Radiobutton>(
				box,
				group,
				int(models.size()),
				tr::lng_tgai_model_other(tr::now)),
			st::boxRowPadding + QMargins(0, Px(4), 0, Px(4)));
		const auto other = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			tr::lng_tgai_model_placeholder(),
			(index == int(models.size())) ? current : QString()));
		other->changes() | rpl::on_next([=] {
			if (!other->getLastText().trimmed().isEmpty()) {
				group->setValue(int(models.size()));
			}
		}, other->lifetime());

		Ui::AddSubsectionTitle(content, tr::lng_tgai_style_title());
		const auto style = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::newGroupDescription,
			Ui::InputField::Mode::MultiLine,
			tr::lng_tgai_style_placeholder(),
			Extera::String(u"tristegram_ai_style"_q)));
		style->setMaxLength(600);

		Ui::AddSubsectionTitle(content, tr::lng_tgai_creative_title());
		const auto creative = std::make_shared<Ui::RadiobuttonGroup>([] {
			const auto value = Extera::Int(u"tristegram_ai_temperature"_q);
			return (value <= 2) ? 0 : (value <= 6) ? 1 : 2;
		}());
		const auto creativeTitles = std::array{
			tr::lng_tgai_creative_exact(tr::now),
			tr::lng_tgai_creative_balanced(tr::now),
			tr::lng_tgai_creative_free(tr::now),
		};
		for (auto i = 0; i != 3; ++i) {
			box->addRow(
				object_ptr<Ui::Radiobutton>(
					box,
					creative,
					i,
					creativeTitles[i]),
				st::boxRowPadding + QMargins(0, Px(4), 0, Px(4)));
		}

		Ui::AddSubsectionTitle(content, tr::lng_tgai_behavior_title());
		const auto enabled = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_tgai_enabled(tr::now),
				Extera::Bool(u"tristegram_ai_writer"_q)),
			st::boxRowPadding + QMargins(0, Px(4), 0, Px(4)));
		const auto preview = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_tgai_preview(tr::now),
				Extera::Bool(u"tristegram_ai_preview"_q)),
			st::boxRowPadding + QMargins(0, Px(4), 0, Px(4)));
		const auto emoji = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				tr::lng_tgai_keep_emoji(tr::now),
				Extera::Bool(u"tristegram_ai_keep_emoji"_q)),
			st::boxRowPadding + QMargins(0, Px(4), 0, Px(4)));
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_tgai_about(),
			st::boxDividerLabel),
			st::boxRowPadding + QMargins(0, Px(10), 0, 0));

		box->setFocusCallback([=] {
			if (Key().isEmpty()) {
				key->setFocusFast();
			}
		});
		box->addButton(tr::lng_settings_save(), [=] {
			auto &config = Extera::Config::Instance();
			config.setString(u"ai_chat_key"_q, key->getLastText().trimmed());
			const auto chosen = group->current();
			const auto model = (chosen >= 0 && chosen < int(models.size()))
				? models[chosen].id
				: other->getLastText().trimmed();
			config.setString(
				u"tristegram_ai_model"_q,
				model.isEmpty() ? models.front().id : model);
			config.setString(
				u"tristegram_ai_style"_q,
				style->getLastText().trimmed());
			const auto temperatures = std::array{ 2, 5, 9 };
			config.setInt(
				u"tristegram_ai_temperature"_q,
				temperatures[std::clamp(creative->current(), 0, 2)]);
			config.setBool(u"tristegram_ai_writer"_q, enabled->checked());
			config.setBool(u"tristegram_ai_preview"_q, preview->checked());
			config.setBool(u"tristegram_ai_keep_emoji"_q, emoji->checked());
			box->closeBox();
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

} // namespace Tristegram
