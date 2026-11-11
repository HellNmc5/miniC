#include "Cst.h"
#include <iostream>

using namespace std;

string cstChain(const CstNode*& n) {
	string s = n->label;
	while (!n->terminal && n->children.size() == 1) {  // один потомок — пишем в ту же строку
		n = n->children[0].get();
		s += " → " + n->label;
	}
	return s;
}

namespace {
	/*
	 prefix — линии от предков, которые ещё продолжаются вниз («│  » или «   »).
	 last   — узел последний у родителя: рисуем «└─» и под ним линию не продолжаем.
	*/
	void printNode(const CstNode* n, const string& prefix, bool last) {
		string text = cstChain(n);                         // n теперь — конец цепочки
		cout << prefix << (last ? "└─ " : "├─ ") << text << "\n";
		const string next = prefix + (last ? "   " : "│  ");
		for (size_t i = 0; i < n->children.size(); ++i)
			printNode(n->children[i].get(), next, i + 1 == n->children.size());
	}
}

void printCst(const CstNode& root) {
	const CstNode* n = &root;
	cout << cstChain(n) << "\n";                           // корень — без линий
	for (size_t i = 0; i < n->children.size(); ++i)
		printNode(n->children[i].get(), "", i + 1 == n->children.size());
}
