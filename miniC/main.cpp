#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include <unordered_map>

using namespace std;



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
	OpDoubleAssi,
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
	string text;
	int line;
	int colStart;
	int colEnd;
	int value; //Номер идентификатора
};

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
	switch (switch_on)
	{
	default:
		break;
	}
}

string className(TokenClass cls) {
	switch (switch_on)
	{
	default:
		break;
	}
}

string readFile(const string& path) {
	//Читка файла и его проверка
	ifstream file(path);

	if (!file.is_open()) {
		return "ГГ - это точно";
	}
	stringstream ss;

	ss << file.rdbuf();
	string file_str = ss.str();
	return file_str;
}

void printSource(const string& text) {
	//Вывод построчно: 1|текст
	istringstream time_text(text);
	string t;

	int i = 1;
	while (getline(time_text, t)) {
		cout << setw(3) << i << " | " << t << endl;
		i++;
	}

}




int main(int args, char* argv[]) {
	//argv[1]  - путь
	setlocale(LC_ALL, "");

	string path = (args > 1) ? argv[1] : "test.mc";

	string text = readFile(path);
	printSource(text);
}