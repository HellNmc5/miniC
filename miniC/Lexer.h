#pragma once
#include <string>

enum class TokenClass {
	Keyword,
	Identificator,
	Const,
	OpSign,
	Separator,
	Mistake

};

enum class TokenCode {
	//Таблица 3
	kwInt = 1,
	kwWord,
	kwBool,
	kwIf,
	kwElse,
	kwWhile,
	kwReturn,
	kwMain,

	//Таблица 4
	Identfier = 9,
	IntConst = 10,
	BoolTrue,
	BoolFalse,

	//Таблица 5
	OpAssign = 13,
	OpPlus,
	OpMinus,
	OpMult,
	OpDiv,
	OpDivPerc,
	OpMoveLeft,
	OpMoveRight,
	OpMenee,
	OpBolee,
	OpMenAssi,
	OpBolAssi,
	OpEq,
	OpNotAssign,
	OpLogAnd,
	OpLogOr,
	OpLogNot,

	//Таблица 6
	LParent = 30,
	RParent,
	LFigParent,
	RFigParent,
	DotComm,
	Comm,

	//Таблица 7
	Error = 36,
	Comment = 37,
	EndOfFile = 38
};

struct Token {
	TokenClass cls;
	TokenCode code;
	std::string text;
	int line;
	int colStart;
	int colEnd;
	int value; //Номер идентификатора
};

std::string codeName(TokenCode code);

std::string className(TokenClass cls);

class Lexer {
public:
	explicit Lexer(const std::string& source);

	bool atEnd() const;//Чек - дошли ли до конца?
	char peek(int ahead = 0) const;//Смотрим символ не двигаясь
	char advance();//воруем символ и двигаем дальше

	int line() const;
	int col() const;

private:
	std::string text;
	size_t pos = 0;//индекс текущего символа
	int ln = 1;//номер строки
	int cl = 1;//номер колонки
};