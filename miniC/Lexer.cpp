#include "Lexer.h"
#include <unordered_map>

using namespace std;

const unordered_map<string, TokenCode> keywords = {
	//Ключевые слова, таблица 3
	{"int",    TokenCode::kwInt},
	{"word",   TokenCode::kwWord},
	{"bool",   TokenCode::kwBool},
	{"if",     TokenCode::kwIf},
	{"else",   TokenCode::kwElse},
	{"while",  TokenCode::kwWhile},
	{"return", TokenCode::kwReturn},
	{"main",   TokenCode::kwMain},
	{"true",   TokenCode::BoolTrue},
	{"false",  TokenCode::BoolFalse}
};

std::string codeName(TokenCode code) {
	switch (code) {
		// Таблица 3 — ключевые слова
	case TokenCode::kwInt:        return "тип int";
	case TokenCode::kwWord:       return "тип word";
	case TokenCode::kwBool:       return "тип bool";
	case TokenCode::kwIf:         return "условный оператор";
	case TokenCode::kwElse:       return "альтернативная ветвь";
	case TokenCode::kwWhile:      return "цикл с предусловием";
	case TokenCode::kwReturn:     return "возврат из функции";
	case TokenCode::kwMain:       return "точка входа";

		// Таблица 4 — идентификаторы и константы
	case TokenCode::Identfier:    return "идентификатор";
	case TokenCode::IntConst:     return "целая константа";
	case TokenCode::BoolTrue:     return "логическая константа";
	case TokenCode::BoolFalse:    return "логическая константа";

		// Таблица 5 — операции
	case TokenCode::OpAssign:     return "присваивание";
	case TokenCode::OpPlus:       return "сложение";
	case TokenCode::OpMinus:      return "вычитание";
	case TokenCode::OpMult:       return "умножение";
	case TokenCode::OpDiv:        return "деление";
	case TokenCode::OpDivPerc:    return "остаток от деления";
	case TokenCode::OpMoveLeft:   return "сдвиг влево";
	case TokenCode::OpMoveRight:  return "сдвиг вправо";
	case TokenCode::OpMenee:      return "меньше";
	case TokenCode::OpBolee:      return "больше";
	case TokenCode::OpMenAssi:    return "меньше или равно";
	case TokenCode::OpBolAssi:    return "больше или равно";
	case TokenCode::OpEq: return "равно";
	case TokenCode::OpNotAssign:  return "не равно";
	case TokenCode::OpLogAnd:     return "логическое И";
	case TokenCode::OpLogOr:      return "логическое ИЛИ";
	case TokenCode::OpLogNot:     return "логическое НЕ";

		// Таблица 6 — разделители
	case TokenCode::LParent:      return "открывающая скобка";
	case TokenCode::RParent:      return "закрывающая скобка";
	case TokenCode::LFigParent:   return "начало блока";
	case TokenCode::RFigParent:   return "конец блока";
	case TokenCode::DotComm:      return "конец оператора";
	case TokenCode::Comm:         return "запятая";

		// Таблица 7 — служебные
	case TokenCode::Error:        return "недопустимая лексема";
	case TokenCode::Comment:      return "комментарий";
	case TokenCode::EndOfFile:    return "конец файла";
	}
	return "?";
}

std::string className(TokenClass cls) {
	switch (cls)
	{
	case TokenClass::Keyword: return "ключевое слово";
	case TokenClass::Identificator: return "идентификатор";
	case TokenClass::Separator: return "разделитель";
	case TokenClass::Const: return "константа";
	case TokenClass::Mistake: return "ошибка";
	case TokenClass::OpSign: return "знак операции";
	}
	return "Не знать иного!";
}
