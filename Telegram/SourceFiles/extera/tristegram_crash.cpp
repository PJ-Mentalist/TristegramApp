/*
This file is part of Tristegram, a client based on exDesktop and
Telegram Desktop. Licensed under GNU GPL v3.
*/
#include "extera/tristegram_crash.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QString>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstdio>
#include <cwchar>
#endif // Q_OS_WIN

namespace Tristegram {
namespace {

#ifdef Q_OS_WIN
wchar_t LogPath[MAX_PATH * 2] = { 0 };
LPTOP_LEVEL_EXCEPTION_FILTER Previous = nullptr;
volatile LONG Writing = 0;

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

LONG WINAPI Filter(EXCEPTION_POINTERS *info) {
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
	return Previous ? Previous(info) : EXCEPTION_CONTINUE_SEARCH;
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
