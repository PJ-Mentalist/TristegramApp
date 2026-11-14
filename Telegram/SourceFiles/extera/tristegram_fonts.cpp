/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_fonts.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "settings.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_session_controller.h"
#include "mainwindow.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtGui/QFontDatabase>

namespace Tristegram {
namespace {

[[nodiscard]] QString FontsFolder() {
	return cWorkingDir() + u"tdata/tristegram_fonts/"_q;
}

} // namespace

void RegisterUserFonts() {
	const auto dir = QDir(FontsFolder());
	if (!dir.exists()) {
		return;
	}
	const auto files = dir.entryInfoList(
		{ u"*.ttf"_q, u"*.otf"_q, u"*.TTF"_q, u"*.OTF"_q },
		QDir::Files);
	for (const auto &file : files) {
		QFontDatabase::addApplicationFont(file.absoluteFilePath());
	}
}

void ChooseUserFont(not_null<Window::SessionController*> controller) {
	const auto weak = base::make_weak(controller);
	FileDialog::GetOpenPath(
		QPointer<QWidget>(controller->widget().get()),
		tr::lng_tristegram_font_choose(tr::now),
		u"Fonts (*.ttf *.otf)"_q,
		[=](FileDialog::OpenResult &&result) {
			const auto strong = weak.get();
			if (!strong || result.paths.isEmpty()) {
				return;
			}
			const auto source = QFileInfo(result.paths.front());
			QDir().mkpath(FontsFolder());
			const auto target = FontsFolder() + source.fileName();
			if (QFile::exists(target)) {
				QFile::remove(target);
			}
			if (!QFile::copy(source.absoluteFilePath(), target)) {
				strong->showToast(tr::lng_tristegram_font_failed(tr::now));
				return;
			}
			const auto id = QFontDatabase::addApplicationFont(target);
			const auto families = (id >= 0)
				? QFontDatabase::applicationFontFamilies(id)
				: QStringList();
			if (families.isEmpty()) {
				QFile::remove(target);
				strong->showToast(tr::lng_tristegram_font_failed(tr::now));
				return;
			}
			Core::App().settings().setCustomFontFamily(families.front());
			Core::App().saveSettings();
			strong->show(Ui::MakeConfirmBox({
				.text = tr::lng_extera_restart_required(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_extera_restart(),
			}));
		});
}

} // namespace Tristegram
