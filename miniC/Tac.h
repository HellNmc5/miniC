#pragma once
#include <string>
#include <vector>
#include "Triads.h"

/*
 Трёхадресный код (TAC) — запись тех же триад в виде команд «x := y op z».
 Генератор не меняется: TriadBuilder обходит сокращённое дерево снизу вверх
 и выдаёт триады, а здесь они только печатаются по-другому:
   результат триады N  → временная переменная tN (вместо ссылки ^N);
   переход на триаду N → метка LN, которая ставится перед этой командой.
*/
std::vector<std::string> tacLines(const std::vector<Triad>& code);

// Вывести трёхадресный код в консоль
void printTac(const std::vector<Triad>& code);
