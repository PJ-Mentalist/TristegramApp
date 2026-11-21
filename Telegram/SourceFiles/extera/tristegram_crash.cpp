/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_crash.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QString>
#include <QtGui/QCursor>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <csignal>
#include <cstdlib>
#include <exception>
#endif // Q_OS_WIN

#include "base/event_filter.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace Tristegram {
namespace {

#ifdef Q_OS_WIN
wchar_t LogPath[MAX_PATH * 2] = { 0 };
LPTOP_LEVEL_EXCEPTION_FILTER Previous = nullptr;
volatile LONG Writing = 0;

// What was clicked last, written into the report.
char LastClick[512] = { 0 };
char LastKey[128] = { 0 };

void Append(HANDLE file, const char *text) {
	auto written = DWORD(0);
	WriteFile(file, text, DWORD(strlen(text)), &written, nullptr);
}

void DescribeAddress(HANDLE file, const void *address) {
	char line[600] = { 0 };
	HMODULE module = nullptr;
	if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
				| GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(address),
			&module) && module) {
		wchar_t path[MAX_PATH] = { 0 };
		GetModuleFileNameW(module, path, MAX_PATH);
		const auto name = wcsrchr(path, L'\\');
		const auto offset = reinterpret_cast<const char*>(address)
			- reinterpret_cast<const char*>(module);
		_snprintf_s(
			line,
			sizeof(line),
			_TRUNCATE,
			"  %ls+0x%llx\r\n",
			name ? (name + 1) : path,
			static_cast<unsigned long long>(offset));
	} else {
		_snprintf_s(
			line,
			sizeof(line),
			_TRUNCATE,
			"  0x%p\r\n",
			address);
	}
	Append(file, line);
}

void WriteReport(const char *reason, EXCEPTION_POINTERS *info) {
	if (InterlockedExchange(&Writing, 1) == 0 && LogPath[0]) {
		const auto file = CreateFileW(
			LogPath,
			GENERIC_WRITE,
			0,
			nullptr,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file != INVALID_HANDLE_VALUE) {
			char line[300] = { 0 };
			auto time = SYSTEMTIME();
			GetLocalTime(&time);
			_snprintf_s(
				line,
				sizeof(line),
				_TRUNCATE,
				"Tristegram crash %04d-%02d-%02d %02d:%02d:%02d\r\n",
				int(time.wYear),
				int(time.wMonth),
				int(time.wDay),
				int(time.wHour),
				int(time.wMinute),
				int(time.wSecond));
			Append(file, line);
			if (reason) {
				Append(file, "Reason: ");
				Append(file, reason);
				Append(file, "\r\n");
			}
			if (LastClick[0]) {
				Append(file, "Last click: ");
				Append(file, LastClick);
				Append(file, "\r\n");
			}
			if (LastKey[0]) {
				Append(file, "Last key: ");
				Append(file, LastKey);
				Append(file, "\r\n");
			}
			if (info && info->ExceptionRecord) {
				const auto record = info->ExceptionRecord;
				_snprintf_s(
					line,
					sizeof(line),
					_TRUNCATE,
					"Code 0x%08lx, thread %lu\r\nAt:\r\n",
					static_cast<unsigned long>(record->ExceptionCode),
					static_cast<unsigned long>(GetCurrentThreadId()));
				Append(file, line);
				DescribeAddress(file, record->ExceptionAddress);
				if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
					&& record->NumberParameters >= 2) {
					_snprintf_s(
						line,
						sizeof(line),
						_TRUNCATE,
						"%s address 0x%llx\r\n",
						record->ExceptionInformation[0] ? "Write" : "Read",
						static_cast<unsigned long long>(
							record->ExceptionInformation[1]));
					Append(file, line);
				}
			}
			Append(file, "Stack:\r\n");
			void *frames[62] = { nullptr };
			const auto count = CaptureStackBackTrace(0, 62, frames, nullptr);
			for (auto i = USHORT(0); i != count; ++i) {
				DescribeAddress(file, frames[i]);
			}
			CloseHandle(file);
		}
	}
}

LONG WINAPI Filter(EXCEPTION_POINTERS *info) {
	WriteReport(nullptr, info);
	return Previous ? Previous(info) : EXCEPTION_CONTINUE_SEARCH;
}

// Crashes that do not go through the filter: abort, terminate, a pure
// virtual call or a bad argument to the C runtime.
void OnAbort(int) {
	WriteReport("abort", nullptr);
	TerminateProcess(GetCurrentProcess(), 3);
}

void OnTerminate() {
	WriteReport("std::terminate (an exception nobody caught)", nullptr);
	TerminateProcess(GetCurrentProcess(), 3);
}

void OnPureCall() {
	WriteReport("pure virtual function call", nullptr);
	TerminateProcess(GetCurrentProcess(), 3);
}

void OnInvalidParameter(
		const wchar_t*,
		const wchar_t*,
		const wchar_t*,
		unsigned int,
		uintptr_t) {
	WriteReport("invalid parameter to the C runtime", nullptr);
	TerminateProcess(GetCurrentProcess(), 3);
}

void CopyText(char *to, size_t size, const QByteArray &text) {
	const auto length = std::min(size_t(text.size()), size - 1);
	memcpy(to, text.constData(), length);
	to[length] = 0;
}
#endif // Q_OS_WIN

[[nodiscard]] QString LogFilePath() {
#ifdef Q_OS_WIN
	return QString::fromWCharArray(LogPath);
#else // Q_OS_WIN
	return QString();
#endif // Q_OS_WIN
}

} // namespace

void InstallCrashLog() {
#ifdef Q_OS_WIN
	auto size = GetModuleFileNameW(nullptr, LogPath, MAX_PATH);
	if (!size || size >= MAX_PATH) {
		LogPath[0] = 0;
		return;
	}
	auto slash = wcsrchr(LogPath, L'\\');
	if (!slash) {
		LogPath[0] = 0;
		return;
	}
	wcscpy_s(
		slash + 1,
		(MAX_PATH * 2) - (slash + 1 - LogPath),
		L"tristegram_crash.txt");
	Previous = SetUnhandledExceptionFilter(Filter);
	signal(SIGABRT, OnAbort);
	std::set_terminate(OnTerminate);
	_set_purecall_handler(OnPureCall);
	_set_invalid_parameter_handler(OnInvalidParameter);
	// Leave room on the stack to write the report after an overflow.
	auto guarantee = ULONG(64 * 1024);
	SetThreadStackGuarantee(&guarantee);
#endif // Q_OS_WIN
}

void InstallCrashBreadcrumbs() {
#ifdef Q_OS_WIN
	static auto installed = false;
	if (installed || !qApp) {
		return;
	}
	installed = true;
	base::install_event_filter(qApp, [](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress
			|| type == QEvent::MouseButtonRelease) {
			// The widget under the mouse and a few of its parents.
			auto text = QByteArray(
				(type == QEvent::MouseButtonPress) ? "press " : "release ");
			auto widget = QApplication::widgetAt(QCursor::pos());
			for (auto i = 0; widget && i != 4; ++i) {
				if (i) {
					text += " < ";
				}
				text += widget->metaObject()->className();
				if (!widget->objectName().isEmpty()) {
					text += " '" + widget->objectName().toUtf8() + "'";
				}
				widget = widget->parentWidget();
			}
			CopyText(LastClick, sizeof(LastClick), text);
		} else if (type == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get());
			CopyText(
				LastKey,
				sizeof(LastKey),
				QKeySequence(key->keyCombination()).toString().toUtf8());
		}
		return base::EventFilterResult::Continue;
	});
#endif // Q_OS_WIN
}

QString TakeLastCrashLog() {
	const auto path = LogFilePath();
	if (path.isEmpty() || !QFile::exists(path)) {
		return QString();
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const auto text = QString::fromUtf8(file.readAll());
	file.close();
	const auto seen = path + u".seen"_q;
	QFile::remove(seen);
	QFile::rename(path, seen);
	return text;
}

} // namespace Tristegram
