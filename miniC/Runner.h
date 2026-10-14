#pragma once
#include <string>

/**
 * @brief Результат сборки и запуска программы.
 */
struct RunResult {
	bool built = false;       // удалось ли собрать .exe
	bool finished = false;    // программа завершилась сама, а не остановлена по времени
	int exitCode = 0;         // код возврата — то, что вернула main
	std::string output;       // что программа напечатала; если сборка не удалась — сообщения NASM и link
};

/**
 * @brief Собрать ассемблерный текст в .exe и запустить его.
 *
 * Шаги: записать .asm в рабочую папку → найти Visual Studio и подготовить
 * окружение x86 (vcvars32.bat) → nasm и link → запустить .exe, собрать
 * его вывод и код возврата. Работает синхронно: интерфейс ждёт, пока всё
 * закончится. Программа, которая работает дольше timeoutMs, принудительно
 * останавливается — на случай бесконечного цикла.
 *
 * @param asmText   текст на ассемблере от generateAsm
 * @param workDir   папка для run.asm, run.obj, run.exe; путь лучше без кириллицы
 * @param timeoutMs сколько ждать программу, в миллисекундах
 */
RunResult buildAndRun(const std::string& asmText, const std::wstring& workDir, unsigned timeoutMs = 5000);
