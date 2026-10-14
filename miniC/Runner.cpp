#ifndef UNICODE
#define UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Runner.h"
#include <windows.h>
#include <string>

using namespace std;

/*
 * Сборка и запуск программы на miniC.
 *
 * Компилятор выдал текст на ассемблере; дальше нужны две внешние программы:
 * NASM превращает его в объектный файл, link из Visual Studio — в .exe.
 * link работает только в окружении, где прописаны пути к библиотекам Windows,
 * поэтому перед ним выполняется vcvars32.bat — тот же файл, что запускает
 * «x86 Native Tools Command Prompt». Где он лежит, подсказывает vswhere —
 * утилита, которую ставит установщик Visual Studio.
 */

namespace {

	constexpr size_t OUTPUT_LIMIT = 1 << 20;   // больше мегабайта вывода не собираем

	// Текст из консольной кодировки (так пишут link и cmd) в UTF-8 для интерфейса
	string oemToUtf8(const string& s) {
		if (s.empty()) return s;
		int n = MultiByteToWideChar(CP_OEMCP, 0, s.data(), (int)s.size(), nullptr, 0);
		wstring w(n, L'\0');
		MultiByteToWideChar(CP_OEMCP, 0, s.data(), (int)s.size(), &w[0], n);
		int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
		string out(m, '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.data(), n, &out[0], m, nullptr, nullptr);
		return out;
	}

	// Убрать \r — в интерфейсе строки разделяются только \n
	string stripCR(string s) {
		string out;
		out.reserve(s.size());
		for (char c : s) if (c != '\r') out += c;
		return out;
	}

	bool fileExists(const wstring& path) {
		return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	/**
	 * @brief Запустить процесс без окна и собрать всё, что он пишет в stdout и stderr.
	 * @param finished false, если процесс не уложился во время и был остановлен
	 * @return false, если процесс вообще не удалось запустить
	 */
	bool runProcess(const wstring& commandLine, const wstring& cwd, unsigned timeoutMs,
	                string& output, int& exitCode, bool& finished) {
		output.clear();
		exitCode = -1;
		finished = false;

		// канал: процесс пишет в wr, мы читаем из rd
		SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
		HANDLE rd = nullptr, wr = nullptr;
		if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
		SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);    // читающий конец процессу не нужен

		STARTUPINFOW si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESTDHANDLES;
		si.hStdOutput = wr;
		si.hStdError = wr;
		PROCESS_INFORMATION pi{};

		wstring cmd = commandLine;                           // CreateProcessW может менять строку
		BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
		                         nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
		CloseHandle(wr);                                     // наш экземпляр пишущего конца больше не нужен
		if (!ok) {
			CloseHandle(rd);
			return false;
		}

		auto readAvailable = [&]() {
			DWORD avail = 0;
			while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
				char buf[4096];
				DWORD got = 0;
				if (!ReadFile(rd, buf, (DWORD)min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0) break;
				if (output.size() < OUTPUT_LIMIT) output.append(buf, got);
			}
		};

		const ULONGLONG deadline = GetTickCount64() + timeoutMs;
		for (;;) {
			readAvailable();                                 // читаем по ходу, иначе канал переполнится и процесс встанет
			if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) {
				finished = true;
				break;
			}
			if (GetTickCount64() > deadline) {
				TerminateProcess(pi.hProcess, 1);
				WaitForSingleObject(pi.hProcess, 1000);
				break;
			}
		}
		readAvailable();                                     // то, что успело прийти перед завершением
		if (output.size() >= OUTPUT_LIMIT) output += "\n… вывод обрезан …";

		DWORD code = 0;
		GetExitCodeProcess(pi.hProcess, &code);
		exitCode = (int)code;

		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		CloseHandle(rd);
		return true;
	}

	wstring utf8ToWide(const string& s) {
		int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
		wstring w(n, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
		return w;
	}

	wstring expand(const wchar_t* s) {
		wchar_t buf[MAX_PATH];
		DWORD n = ExpandEnvironmentStringsW(s, buf, MAX_PATH);
		return (n > 0 && n <= MAX_PATH) ? wstring(buf) : wstring();
	}

	/**
	 * @brief Найти vcvars32.bat.
	 *
	 * Сначала спрашиваем vswhere про все установленные студии (и полные, и Build Tools)
	 * и берём первую, где этот файл есть. Если vswhere нет или он ничего не дал —
	 * проверяем обычные папки установки.
	 * @param log сюда пишется, что проверялось, — для сообщения об ошибке
	 * @return полный путь или пустая строка
	 */
	wstring findVcvars32(string& log) {
		const wstring tail = L"\\VC\\Auxiliary\\Build\\vcvars32.bat";

		const wstring vswhere = expand(L"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe");
		if (fileExists(vswhere)) {
			string out;
			int code = 0;
			bool finished = false;
			const wstring cmd = L"\"" + vswhere + L"\" -all -prerelease -products * -utf8 -property installationPath";
			if (runProcess(cmd, L"", 10000, out, code, finished) && code == 0) {
				size_t pos = 0;
				while (pos < out.size()) {                   // по строке на каждую установку
					size_t end = out.find_first_of("\r\n", pos);
					if (end == string::npos) end = out.size();
					string line = out.substr(pos, end - pos);
					pos = end + 1;
					if (line.empty()) continue;
					log += "  vswhere: " + line + "\n";
					const wstring bat = utf8ToWide(line) + tail;
					if (fileExists(bat)) return bat;
				}
			}
			else log += "  vswhere не ответил\n";
		}
		else log += "  vswhere.exe не найден\n";

		// запасной вариант — стандартные папки
		const wchar_t* roots[] = { L"%ProgramFiles%\\Microsoft Visual Studio\\", L"%ProgramFiles(x86)%\\Microsoft Visual Studio\\" };
		const wchar_t* versions[] = { L"18", L"2022", L"2019", L"2017" };
		const wchar_t* editions[] = { L"Community", L"Professional", L"Enterprise", L"BuildTools", L"Preview" };
		for (const wchar_t* root : roots)
			for (const wchar_t* ver : versions)
				for (const wchar_t* ed : editions) {
					const wstring bat = expand(root) + ver + L"\\" + ed + tail;
					if (fileExists(bat)) return bat;
				}
		log += "  в стандартных папках Visual Studio vcvars32.bat тоже нет\n";
		return L"";
	}

	// Если NASM стоит в обычной папке, но её нет в PATH, — добавить для дочерних процессов
	void addNasmToPath() {
		const wstring nasmDir = L"C:\\Program Files\\NASM";
		if (!fileExists(nasmDir + L"\\nasm.exe")) return;
		DWORD n = GetEnvironmentVariableW(L"PATH", nullptr, 0);
		wstring path(n, L'\0');
		GetEnvironmentVariableW(L"PATH", &path[0], n);
		if (path.find(nasmDir) == wstring::npos)
			SetEnvironmentVariableW(L"PATH", (path.c_str() + wstring(L";") + nasmDir).c_str());
	}
}

RunResult buildAndRun(const string& asmText, const wstring& workDir, unsigned timeoutMs) {
	RunResult r;
	CreateDirectoryW(workDir.c_str(), nullptr);

	// 1. записать ассемблер, убрать старые результаты — чтобы при ошибке не запустился прошлый .exe
	const wstring asmPath = workDir + L"\\run.asm";
	DeleteFileW((workDir + L"\\run.obj").c_str());
	DeleteFileW((workDir + L"\\run.exe").c_str());
	HANDLE f = CreateFileW(asmPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE) {
		r.output = "Не удалось записать файл run.asm в рабочую папку.";
		return r;
	}
	DWORD written = 0;
	WriteFile(f, asmText.data(), (DWORD)asmText.size(), &written, nullptr);
	CloseHandle(f);

	string log;
	int code = 0;
	bool finished = false;

	// 2. NASM: run.asm → run.obj
	addNasmToPath();
	if (!runProcess(L"nasm -f win32 run.asm -o run.obj", workDir, 30000, log, code, finished)) {
		r.output = "Не найден NASM. Установите его (nasm.us) и добавьте папку с nasm.exe в PATH.";
		return r;
	}
	if (code != 0 || !fileExists(workDir + L"\\run.obj")) {
		r.output = "Ошибка ассемблера NASM:\n" + stripCR(oemToUtf8(log));
		return r;
	}

	// 3. link: run.obj → run.exe, в окружении Visual Studio (vcvars32.bat)
	string searchLog;
	const wstring vcvars = findVcvars32(searchLog);
	if (vcvars.empty()) {
		r.output = "Не найден vcvars32.bat: для компоновщика link нужна Visual Studio\n"
		           "или Build Tools с компонентом «Разработка классических приложений на C++».\n"
		           "Что проверялось:\n" + searchLog;
		return r;
	}
	const wstring link =
		L"cmd.exe /s /c \"call \"" + vcvars + L"\" >nul"
		L" & link /nologo run.obj kernel32.lib /subsystem:console /entry:start /out:run.exe\"";
	if (!runProcess(link, workDir, 60000, log, code, finished)) {
		r.output = "Не удалось запустить cmd.exe для компоновки.";
		return r;
	}
	if (code != 0 || !fileExists(workDir + L"\\run.exe")) {
		r.output = "Ошибка компоновщика link:\n" + stripCR(oemToUtf8(log));
		return r;
	}
	r.built = true;

	// 4. запуск
	string out;
	if (!runProcess(L"\"" + workDir + L"\\run.exe\"", workDir, timeoutMs, out, r.exitCode, r.finished)) {
		r.output = "Не удалось запустить run.exe.";
		return r;
	}
	r.output = stripCR(out);
	return r;
}
