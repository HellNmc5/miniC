#pragma once
#include <memory>
#include <string>
#include <vector>
#include "Lexer.h"

enum class NodeKind {//перечисления видов узлов(TokenCode - слова, NodeKind - предложения
	Program, Function, Param, Block, //Структура программы
	VarDecl, Assign, Call , If, While, Return, //Операторы
	Binary, Unary, Const, Var // Выражения
};

struct Node//Узел дерева разрбора 
{
	NodeKind kind;//Тип узла: функция, условия, операция

	Token token; //Главная лексема узла(ради чего существует узел): Имя, операция, константа(ориентир при построении дерева)
	//даёт текст и позицию в исходнике для сообщений об ошибках

	TokenCode type{};//Тип для Function, Param, VarDecl: kwInt, kwWord или kwBool
	std::vector<std::unique_ptr<Node>> children;// дочерние узлы; порядок задан таблицей узлов
};

using NodePtr = std::unique_ptr<Node>;//Короткое имя: Указатель на конкретный объект, т.е. он единственный представитель данной структуры
//Проще: сокращение записи при написании методов. unique_ptr - используется для самоотчистки наследников и наш объект не содержал сам себя
//Удалили корень - минус все дерево

NodePtr makeNode(NodeKind kind, const Token& token);//Создание узла
std::string nodeName(NodeKind kind);//название вида узла для вывода
void printTree(const Node& node, int depth = 0);//Вывод дерева