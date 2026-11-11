#pragma once
#include <string>
#include "Ast.h"
#include "Cst.h"
#include "Lexer.h"

struct SyntaxError
{
	std::string message;
	int line, col;
};

class Parser
{
public:
	explicit Parser(const std::string& source);
	NodePtr parseProgram(); //Разбор программы целиком; ошибки не бросаются наружу, а собираются в список
	const std::vector<SyntaxError>& errors() const { return errorList; } //Все найденные синтаксические ошибки
	CstPtr takeCst() { return std::move(cstRoot); } //Забрать дерево разбора (CST); вызывать после parseProgram
	
	
private:
	Lexer lexer;
	Token cur;//Текущая лексема
	std::vector<SyntaxError> errorList;//Найденные ошибки

	/*
	 Дерево разбора (CST). Строится попутно с сокращённым деревом:
	 каждая функция разбора открывает узел своего нетерминала (CstScope),
	 а advance() добавляет забранную лексему листом в открытый сейчас узел.
	*/
	CstPtr cstRoot;                     //корень — узел Программа
	std::vector<CstNode*> cstStack;     //открытые узлы; back() — тот, куда идут новые листья
	bool recovering = false;            //во время восстановления пропущенные лексемы в дерево не пишем
	void cstLeaf(const Token& t);       //лексему — листом в текущий узел

	/*
	 Узел нетерминала на время работы функции разбора.
	 Конструктор создаёт узел и делает его текущим, деструктор возвращает предыдущий.
	 Деструктор срабатывает и при исключении, поэтому стек не ломается после ошибки.
	*/
	struct CstScope {
		Parser& p;
		CstScope(Parser& parser, const char* name);
		~CstScope();
	};

	/*
	 Узел, который создаётся задним числом — только если конструкция действительно есть.
	 Например, узел «Сумма» нужен, только когда после левого операнда встретился «+» или «-»;
	 иначе уровень не показывается, и дерево не обрастает цепочками пустых уровней.
	 wrap() переносит узлы, добавленные в текущий узел начиная с отметки mark, в новый узел
	 и делает его текущим; деструктор возвращает предыдущий (и при исключении тоже).
	*/
	struct CstLift {
		Parser& p;
		bool active = false;
		explicit CstLift(Parser& parser) : p(parser) {}
		void wrap(const char* name, size_t mark);
		~CstLift();
	};
	size_t cstMark() const;             //сколько потомков сейчас у текущего узла — отметка для CstLift

	void report(const SyntaxError& e);//Записать ошибку в список (повтор на том же месте не пишем)
	void syncStatement();//Восстановление: пропустить лексемы до конца оператора
	void syncFunction();//Восстановление: пропустить лексемы до начала следующей функции

	
	bool checkAny(const std::vector<TokenCode>& codes) const;
	void advance();//переход на следующую лексему
	bool check(TokenCode code) const;//Проверка вида лексемы
	bool match(TokenCode code);//Если подходит, то забираем и возвращаем true
	Token expect(TokenCode code, const std::string& what);//Ожидание|Реальность - обязана быть такой лексемой иначе ошибка

	static NodePtr makeBinary(const Token& op, NodePtr left, NodePtr right);

	NodePtr parseBinary(size_t level);
	NodePtr parseUnary();
	NodePtr parsePrimary();
	
	NodePtr parseVarDecl();
	NodePtr parseIf();
	NodePtr parseWhile();
	NodePtr parseReturn();
	NodePtr parseBlock();
	void parseArguments(Node& call);

	TokenCode expectType(const std::string& what);

	NodePtr parseExpression();
	NodePtr parseStatement();
	
	NodePtr parseFunction();
	NodePtr parseParam();
	// [[noreturn]] — управление не возвращается в место вызова: функция всегда бросает исключение.
	// Без этой пометки компилятор предупреждает «не все пути возвращают значение»
	// в методах, которые заканчиваются вызовом error (например, expect).
	[[noreturn]] void error(const std::string& message);//бросить SyntaxError на текущей лексеме
};
