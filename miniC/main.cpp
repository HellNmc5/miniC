#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include "Lexer.h"
#include "Ast.h"
#include "Parser.h"


using namespace std;

string readFile(const string& path) {
	//Читка файла и его проверка
	ifstream file(path);

	if (!file.is_open()) {
		return "";
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
	SetConsoleOutputCP(CP_UTF8);

	//string path = (args > 1) ? argv[1] : "test.mc";

	//string text = readFile(path);
	//printSource(text);

	//Lexer lx(text);
	//while (true) {
	//	Token t = lx.nextToken();
	//	if (t.code == TokenCode::EndOfFile) break;
	//	cout << className(t.cls) << " - " << t.text << " - строка " << t.line
	//		<< ", с " << t.colStart << " по " << t.colEnd << " символ\n";
	//}
	// --- проверка разбора выражений ---
	const char* tests[] = {
		"2 + 3 * 4",
		"(2 + 3) * 4",
		"a - b - c",
		"-x * 2",
		"!a && b || c",
		"x << 1 <= y",
		"f(1, a + 2) * 3",
		"f()",
		"g() + 1",
		// дальше — с ошибками
		"2 +",
		"(2 + 3",
		"f(1 2)"
	};
	for (const char* src : tests) {
		cout << "== " << src << "\n";
		try {
			Parser p(src);
			printTree(*p.parseExpression());
		}
		catch (const SyntaxError& e) {
			cout << "ошибка: " << e.message << " (поз. " << e.col << ")\n";
		}
		cout << "\n";
	}
	return 0;
	// --- конец проверки ---
}
//git add .
//git commit -m "" -m
//git push