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

string codeName(TokenCode code) {
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

string className(TokenClass cls) {
	switch (cls)
	{
	case TokenClass::Keyword: return "ключевое слово";
	case TokenClass::Identificator: return "идентификатор";
	case TokenClass::Separator: return "разделитель";
	case TokenClass::Const: return "константа";
	case TokenClass::Mistake: return "ошибка";
	case TokenClass::OpSign: return "знак операции";
	case TokenClass::Service: return "служебный";
	}
	return "Не знать иного!";
}

Lexer::Lexer(const string& source) : text(source) {

}

bool Lexer::atEnd() const {
	//pos дошел ли до text.size()
	return pos >= text.size();
}

char Lexer::peek(int ahead) const {
	//Если pos+ahead >= конца, то вернем '\0', иначе text[pos+ahead]
	return (pos + ahead >= text.size()) ? '\0' : text[pos + ahead];
}

char Lexer::advance() {
	//Берем text[pos], двигаем pos
	//'\n', то ln++,cl =1, иначе cl++
	//вернем взятый символ
	if (atEnd()) return '\0';

	char ch = text[pos++];

	if (ch == '\n') {
		ln++;
		cl = 1;
	}
	else {
		cl++;
	}
	return ch;
}

void Lexer::skipSpace() {
	while (!atEnd()) {
		if (isSpace(peek())) {
			advance();
		}
		else if(peek() == '/' && peek(1) == '/') {
			while (!atEnd() && peek() != '\n') {
				advance();
			}
		}
		else {
			return;
		}
	}
}

bool Lexer::isLetter(char c) {
	return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
	
}

bool Lexer::isDigit(char c) {
	return c >= '0' && c <= '9';
	
}

bool Lexer::isSpace(char c) {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

Token Lexer::nextToken() {
	skipSpace();

	int startLine = ln;
	int startCol = cl;

	

	if (atEnd()) {
		// вернуть лексему makeToken
		return makeToken(TokenClass::Service, TokenCode::EndOfFile, "", startLine, startCol);
	}

	char c = peek();

	if (isLetter(c)) {
		//идентификатор/ключСлово
		string word;
		while (isLetter(peek()) || isDigit(peek())) {
			word += advance();
		}
		auto t = keywords.find(word);

		if (t == keywords.end()) {
			//Если в словаре нет, то обычный идентификатор
			return makeToken(TokenClass::Identificator, TokenCode::Identfier, word, startLine, startCol);
		}

		TokenCode code = t->second;

		if (code == TokenCode::BoolTrue) {
			return makeToken(TokenClass::Const, code, word, startLine, startCol, 1);
		}
		if (code == TokenCode::BoolFalse) {
			return makeToken(TokenClass::Const, code, word, startLine, startCol, 0);
		}
		return makeToken(TokenClass::Keyword, code, word, startLine, startCol);
	}
	if (isDigit(c)) {
		// константа — следующим заходом
		string word;
		while (isDigit(peek())) {
			word += advance();

		}
		return makeToken(TokenClass::Const, TokenCode::IntConst, word, startLine, startCol, stoi(word));
	}

	// всё остальное пока — ошибка: съесть символ, вернуть Error
	string bad(1, advance());
	return makeToken(TokenClass::Mistake, TokenCode::Error, bad, startLine, startCol);
	
}

Token Lexer::makeToken(TokenClass cls, TokenCode code, const string& text,
	int line, int colStart, int value) {
	Token t;
	t.cls = cls;
	t.code = code;
	t.text = text;
	t.line = line;
	t.colStart = colStart;
	int len = static_cast<int>(text.size());

	if (len == 0) {
		t.colEnd = colStart;
	}
	else {
		t.colEnd = colStart + len - 1;
	};

	t.value = value;
	return t;
}

int Lexer::line() const { return ln; }
int Lexer::col() const { return cl; }
