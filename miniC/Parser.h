#pragma once
#include <string>
#include "Ast.h"
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
	NodePtr parseProgram(); //Разбор программы целиком, если возникает ошибка бросаем исключение SyntaxError
	
	NodePtr parseExpression();
private:
	Lexer lexer;
	Token cur;//Текущая лексема

	
	bool checkAny(const std::vector<TokenCode>& codes) const;
	void advance();//переход на следующую лексему
	bool check(TokenCode code) const;//Проверка вида лексемы
	bool match(TokenCode code);//Если подходит, то забираем и возвращаем true
	Token expect(TokenCode code, const std::string& what);//Ожидание|Реальность - обязана быть такой лексемой иначе ошибка

	static NodePtr makeBinary(const Token& op, NodePtr left, NodePtr right);

	NodePtr parseBinary(size_t level);
	NodePtr parseUnary();
	NodePtr parsePrimary();

	// [[noreturn]] — управление не возвращается в место вызова: функция всегда бросает исключение.
	// Без этой пометки компилятор предупреждает «не все пути возвращают значение»
	// в методах, которые заканчиваются вызовом error (например, expect).
	[[noreturn]] void error(const std::string& message);//бросить SyntaxError на текущей лексеме
};
