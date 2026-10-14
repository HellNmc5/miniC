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
#include "Semantic.h"
#include "Triads.h"
#include "Optimizer.h"
#include "CodeGen.h"

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
	SetConsoleOutputCP(CP_UTF8);

	string path = (args > 1) ? argv[1] : "test.mc";   // путь из аргумента, иначе test.mc
	string text = readFile(path);
	if (text.empty()) {
		cout << "Не удалось открыть файл: " << path << "\n";
		return 1;
	}

	try {
		Parser p(text);
		NodePtr tree = p.parseProgram();

		Semantic sem;
		vector<SemanticError> errs = sem.check(*tree);
		for (const auto& e : errs)
			cout << "семантика, строка " << e.line << ", позиция " << e.col << ": " << e.message << "\n";

				if (!errs.empty()) return 1;            // с семантическими ошибками код не строим

		if (args > 2 && string(argv[2]) == "--tree")
			printTree(*tree);                    // дерево — только по просьбе

		TriadBuilder builder;
		vector<Triad> code = builder.build(*tree);
		cout << "--- триады ---\n";
		printTriads(code);

		int folded = foldConstants(code);
		cout << "\n--- после свёртки констант (свёрнуто: " << folded << ") ---\n";
		printTriads(code);
		string asmPath = path.substr(0, path.find_last_of('.')) + ".asm";
		ofstream(asmPath) << generateAsm(code);
		cout << "\nассемблер записан в " << asmPath << "\n";
	}
	catch (const SyntaxError& e) {
		cout << "синтаксис, строка " << e.line << ", позиция " << e.col << ": " << e.message << "\n";
	}
	return 0;
}
//git add .
//git commit -m "" -m
//git push