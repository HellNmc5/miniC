#include "Tac.h"
#include <iostream>
#include <set>

using namespace std;

namespace {
	// Знак операции для «tN := a op b» — по порядку TriadOp от Add до Or
	const char* binarySign(TriadOp op) {
		switch (op) {
		case TriadOp::Add: return "+";
		case TriadOp::Sub: return "-";
		case TriadOp::Mul: return "*";
		case TriadOp::Div: return "/";
		case TriadOp::Mod: return "%";
		case TriadOp::Shl: return "<<";
		case TriadOp::Shr: return ">>";
		case TriadOp::Lt:  return "<";
		case TriadOp::Gt:  return ">";
		case TriadOp::Le:  return "<=";
		case TriadOp::Ge:  return ">=";
		case TriadOp::Eq:  return "==";
		case TriadOp::Ne:  return "!=";
		case TriadOp::And: return "&&";
		case TriadOp::Or:  return "||";
		default:           return nullptr;               // не двухместная операция
		}
	}

	/*
	 Операнд триады → текст в трёхадресном коде:
	 константа — число, переменная и функция — имя, ссылка ^N — временная tN, цель перехода N — метка LN.
	*/
	string operand(const Operand& o) {
		switch (o.kind) {
		case OperandKind::Const:  return to_string(o.value);
		case OperandKind::Var:
		case OperandKind::Func:   return o.decl->token.text;
		case OperandKind::Ref:    return "t" + to_string(o.value);
		case OperandKind::Target: return "L" + to_string(o.value);
		default:                  return "";
		}
	}

	// Заголовок функции: «proc fact(n)» — имена параметров берутся из узлов Param функции
	string procHeader(const Operand& f) {
		string s = "proc " + f.decl->token.text + "(";
		bool first = true;
		for (const auto& c : f.decl->children) {
			if (c->kind != NodeKind::Param) continue;
			if (!first) s += ", ";
			s += c->token.text;
			first = false;
		}
		return s + ")";
	}

	// Одна триада → одна команда трёхадресного кода. n — номер триады (с 1)
	string command(const Triad& t, int n) {
		const string res = "t" + to_string(n);           // временная переменная для результата
		if (const char* sign = binarySign(t.op))
			return res + " := " + operand(t.a) + " " + sign + " " + operand(t.b);
		switch (t.op) {
		case TriadOp::Neg:    return res + " := -" + operand(t.a);
		case TriadOp::Not:    return res + " := !" + operand(t.a);
		case TriadOp::C:      return res + " := " + operand(t.a);       // константа после свёртки
		case TriadOp::Assign: return operand(t.a) + " := " + operand(t.b);
		case TriadOp::Param:  return "param " + operand(t.a);
		case TriadOp::Call:   return res + " := call " + operand(t.a) + ", " + operand(t.b);
		case TriadOp::Print:  return "print " + operand(t.a);
		case TriadOp::Jmp:    return "goto " + operand(t.a);
		case TriadOp::Jf:     return "ifFalse " + operand(t.a) + " goto " + operand(t.b);
		case TriadOp::Ret:    return "return " + operand(t.a);
		case TriadOp::Proc:   return procHeader(t.a);
		default:              return "?";
		}
	}
}

vector<string> tacLines(const vector<Triad>& code) {
	// 1. Номера триад, на которые есть переходы, — перед ними нужны метки
	set<int> labels;
	for (const Triad& t : code) {
		if (t.a.kind == OperandKind::Target) labels.insert(t.a.value);
		if (t.b.kind == OperandKind::Target) labels.insert(t.b.value);
	}

	// 2. Команды по порядку; метка — отдельной строкой перед своей командой
	vector<string> lines;
	for (size_t i = 0; i < code.size(); ++i) {
		const int n = (int)i + 1;
		if (code[i].op == TriadOp::Proc && !lines.empty()) lines.push_back("");   // пустая строка между функциями
		if (labels.count(n)) lines.push_back("L" + to_string(n) + ":");
		lines.push_back((code[i].op == TriadOp::Proc ? "" : "    ") + command(code[i], n));
	}
	if (labels.count((int)code.size() + 1)) lines.push_back("L" + to_string(code.size() + 1) + ":");
	return lines;
}

void printTac(const vector<Triad>& code) {
	for (const string& s : tacLines(code)) cout << s << "\n";
}
