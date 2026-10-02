#include "Ast.h"
#include <unordered_map>
#include <iostream>

using namespace std;

namespace {
	const unordered_map<NodeKind, string> pairNode = {
		{NodeKind::Program,"Программа"},
		{NodeKind::Function,"Функция"},
		{NodeKind::Param,"Параметр"},
		{NodeKind::Block,"Блок"},
		{NodeKind::VarDecl,"Объявление"},
		{NodeKind::Assign,"Присваивание"},
		{NodeKind::Call,"Вызов"},
		{NodeKind::If,"Условие"},
		{NodeKind::While,"Цикл"},
		{NodeKind::Return,"Возврат"},
		{NodeKind::Binary,"Операция"},
		{NodeKind::Unary,"Унарная операция"},
		{NodeKind::Const,"Константа"},
		{NodeKind::Var,"Переменная"}
	};
}

NodePtr makeNode(NodeKind kind, const Token& token) {
	NodePtr n(new Node());
	n->kind = kind;
	n->token = token;
	return n;
}

string nodeName(NodeKind kind) {
	auto t = pairNode.find(kind);//Получаем указатель в таблице
	return t == pairNode.end() ? "?" : t->second;//Проверяем, если есть строки нет, то вернем ?, иначе если есть, то вернем второе значение
}

//Временный вывод в консоль дерева
void printTree(const Node& node, int depth) {
	//Отступ
	cout << string(depth * 2, ' ');
	//Название узла и лексема
	cout << nodeName(node.kind);
	if (!node.token.text.empty()) {
		cout << " " << node.token.text;
	}
	//Тип данных если задан
	if (node.type != TokenCode{}) {
		cout << " : " << codeName(node.type);
	}

	cout << '\n';
	//Вызов метода рекурсивно для детей(дочерние узлы дерева)
	for (const auto& child : node.children) {
		printTree(*child, depth + 1);
	}
}