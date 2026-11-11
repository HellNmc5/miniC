#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Lexer.h"

/*
 Узел дерева разбора (CST — concrete syntax tree).
 В отличие от сокращённого дерева (Node из Ast.h), здесь есть узел на каждый
 нетерминал грамматики и лист на каждую лексему, включая скобки и «;».
 Нетерминал: label = "Функция", "Тип" ... , terminal = false.
 Терминал:   label = текст лексемы ("int", "(", "fact"), terminal = true, token — сама лексема.
*/
struct CstNode {
	std::string label;
	bool terminal = false;
	Token token{};                                     // только для терминала: строка, позиция, класс
	std::vector<std::unique_ptr<CstNode>> children;
};

using CstPtr = std::unique_ptr<CstNode>;

/*
 Подпись узла вместе с цепочкой единственных потомков: «Тип → int».
 Так дерево записывается и на доске: пока у узла один потомок, он пишется
 в ту же строку через стрелку. n сдвигается на последний узел цепочки —
 дальше выводятся уже его потомки.
*/
std::string cstChain(const CstNode*& n);

// Вывод дерева в консоль с линиями ├─ └─ │
void printCst(const CstNode& root);
