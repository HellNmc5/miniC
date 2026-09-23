#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>
#include "Lexer.h"

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

	string path = (args > 1) ? argv[1] : "test.mc";

	string text = readFile(path);
	printSource(text);

	Token t{ TokenClass::Keyword, TokenCode::kwWhile, "while", 2, 5, 9, 0 };
	cout << className(t.cls) << " - " << t.text << " - строка " << t.line
		<< ", с " << t.colStart << " по " << t.colEnd << " символ\n";
}
//git add .
//git commit -m ""
//git push