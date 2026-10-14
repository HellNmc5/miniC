#pragma once
#include <string>
#include <vector>
#include "Triads.h"

/**
 * @brief Генерация ассемблера x86 (NASM, 32-битная Windows) из триад.
 *
 * На выходе — текст полной программы: функции miniC, точка входа _start,
 * которая вызывает main и передаёт её результат в ExitProcess как код
 * возврата, и подпрограммы вывода для print.
 *
 * Сборка готового файла:
 *   nasm -f win32 prog.asm -o prog.obj
 *   link prog.obj kernel32.lib /subsystem:console /entry:start /out:prog.exe
 *
 * @param code триады после свёртки констант
 * @return текст на ассемблере
 */
std::string generateAsm(const std::vector<Triad>& code);
