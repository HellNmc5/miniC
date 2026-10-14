#include "CodeGen.h"
#include <set>
#include <sstream>
#include <unordered_map>

using namespace std;

/*
 * Как устроен сгенерированный код.
 *
 * Значения. Типы int и word 16-битные, но вычисления идут в 32-битных
 * регистрах. После каждой операции результат приводится к своему типу:
 * int — расширяется со знаком (movsx), word — без знака (movzx). Так любое
 * значение int лежит в диапазоне -32768..32767, а word — 0..65535, и оба
 * точно помещаются в 32-битное число со знаком. Поэтому сравнение и деление
 * одинаково делаются знаковыми командами для обоих типов, а переполнение
 * 16 бит происходит так же, как в свёртке констант.
 *
 * Память функции — кадр стека, адресуется от регистра ebp:
 *   [ebp+8], [ebp+12], ...  параметры (их положили вызывающие команды push)
 *   [ebp+4]                 адрес возврата
 *   [ebp]                   сохранённый ebp вызывающей функции
 *   [ebp-4], [ebp-8], ...   локальные переменные, затем временные значения
 * Каждой триаде, у которой есть результат, достаётся своя временная ячейка:
 * результат кладётся туда, а ссылка ^n читает его оттуда. Код получается
 * не самым коротким, зато каждая триада переводится сама по себе.
 *
 * Вызов функции: аргументы кладутся в стек по порядку (триады param),
 * затем call; результат возвращается в eax; стек от аргументов очищает
 * вызывающая сторона.
 */

namespace {

	// Операции, у которых есть результат — им нужна временная ячейка
	bool hasResult(TriadOp op) {
		return (op >= TriadOp::Add && op <= TriadOp::Neg) || op == TriadOp::Call;
	}

	// Команда setcc для каждого сравнения: записать 1 в al, если условие выполнено
	const unordered_map<TriadOp, const char*> setcc = {
		{ TriadOp::Lt, "setl" }, { TriadOp::Gt, "setg" },
		{ TriadOp::Le, "setle" }, { TriadOp::Ge, "setge" },
		{ TriadOp::Eq, "sete" }, { TriadOp::Ne, "setne" },
	};

	// Шапка файла, точка входа и подпрограммы вывода — одинаковые для любой программы
	const char* const prologue =
		"; Сгенерировано компилятором miniC\n"
		"; nasm -f win32 prog.asm -o prog.obj\n"
		"; link prog.obj kernel32.lib /subsystem:console /entry:start /out:prog.exe\n"
		"\n"
		"global _start\n"
		"extern _ExitProcess@4, _GetStdHandle@4, _WriteFile@20\n"
		"\n"
		"section .data\n"
		"s_true  db \"true\", 13, 10\n"
		"s_false db \"false\", 13, 10\n"
		"\n"
		"section .text\n"
		"\n"
		"; Точка входа: вызвать main, её результат — код возврата программы\n"
		"_start:\n"
		"    call    f_main\n"
		"    push    eax\n"
		"    call    _ExitProcess@4\n";

	const char* const runtime =
		"\n"
		"; ---------------------------------------------------------------\n"
		"; Подпрограммы вывода для print\n"
		"; ---------------------------------------------------------------\n"
		"\n"
		"; rt_write(адрес, длина) — вывести байты в стандартный вывод\n"
		"rt_write:\n"
		"    push    ebp\n"
		"    mov     ebp, esp\n"
		"    sub     esp, 4                  ; [ebp-4] — сколько байт записано\n"
		"    push    -11                     ; STD_OUTPUT_HANDLE\n"
		"    call    _GetStdHandle@4\n"
		"    push    0                       ; lpOverlapped\n"
		"    lea     ecx, [ebp-4]\n"
		"    push    ecx                     ; lpNumberOfBytesWritten\n"
		"    push    dword [ebp+12]          ; длина\n"
		"    push    dword [ebp+8]           ; адрес\n"
		"    push    eax                     ; дескриптор вывода\n"
		"    call    _WriteFile@20\n"
		"    leave\n"
		"    ret\n"
		"\n"
		"; rt_print_int: вывести число из eax в десятичном виде и перевести строку\n"
		"rt_print_int:\n"
		"    push    ebp\n"
		"    mov     ebp, esp\n"
		"    sub     esp, 16                 ; буфер для цифр, заполняется с конца\n"
		"    push    ebx\n"
		"    push    esi\n"
		"    push    edi\n"
		"    lea     edi, [ebp-2]\n"
		"    mov     word [edi], 0x0A0D      ; \\r\\n в конце строки\n"
		"    mov     ecx, 2                  ; длина текста\n"
		"    mov     ebx, eax                ; запомнить знак\n"
		"    test    eax, eax\n"
		"    jns     .digits\n"
		"    neg     eax                     ; дальше работаем с модулем\n"
		".digits:\n"
		"    xor     edx, edx\n"
		"    mov     esi, 10\n"
		"    div     esi                     ; eax = eax / 10, edx = последняя цифра\n"
		"    add     dl, '0'\n"
		"    dec     edi\n"
		"    mov     [edi], dl\n"
		"    inc     ecx\n"
		"    test    eax, eax\n"
		"    jnz     .digits\n"
		"    test    ebx, ebx\n"
		"    jns     .out\n"
		"    dec     edi\n"
		"    mov     byte [edi], '-'\n"
		"    inc     ecx\n"
		".out:\n"
		"    push    ecx\n"
		"    push    edi\n"
		"    call    rt_write\n"
		"    add     esp, 8\n"
		"    pop     edi\n"
		"    pop     esi\n"
		"    pop     ebx\n"
		"    leave\n"
		"    ret\n"
		"\n"
		"; rt_print_bool: вывести true или false по значению eax\n"
		"rt_print_bool:\n"
		"    test    eax, eax\n"
		"    jz      .false\n"
		"    push    6\n"
		"    push    s_true\n"
		"    jmp     .write\n"
		".false:\n"
		"    push    7\n"
		"    push    s_false\n"
		".write:\n"
		"    call    rt_write\n"
		"    add     esp, 8\n"
		"    ret\n";

	class Generator {
	public:
		explicit Generator(const vector<Triad>& code) : code(code) {}

		string run() {
			// номера триад, на которые ведут переходы, — перед ними нужны метки
			for (const Triad& t : code)
				if (t.op == TriadOp::Jmp) targets.insert(t.a.value);
				else if (t.op == TriadOp::Jf) targets.insert(t.b.value);

			out << prologue;
			for (size_t i = 0; i < code.size(); ) {
				size_t end = i + 1;                         // функция — от своего proc до следующего
				while (end < code.size() && code[end].op != TriadOp::Proc) ++end;
				function(i, end);
				i = end;
			}
			out << runtime;
			return out.str();
		}

	private:
		const vector<Triad>& code;
		ostringstream out;
		set<int> targets;

		// для текущей функции:
		unordered_map<const Node*, int> varOffset;   // объявление → смещение от ebp
		unordered_map<int, int> tempOffset;           // номер триады → смещение её временной ячейки
		TokenCode returnType{};

		void cmd(const string& op, const string& args = "", const string& comment = "") {
			string line = "    " + op;
			if (!args.empty()) line += string(op.size() < 8 ? 8 - op.size() : 1, ' ') + args;
			if (!comment.empty()) line += string(line.size() < 40 ? 40 - line.size() : 1, ' ') + "; " + comment;
			out << line << "\n";
		}

		static string mem(int offset) {
			return offset >= 0 ? "dword [ebp+" + to_string(offset) + "]"
			                   : "dword [ebp-" + to_string(-offset) + "]";
		}

		static string label(int number) { return "T" + to_string(number); }
		static string funcLabel(const Node* fn) { return "f_" + fn->token.text; }

		// Загрузить операнд в регистр
		void load(const string& reg, const Operand& o) {
			switch (o.kind) {
			case OperandKind::Const: cmd("mov", reg + ", " + to_string(o.value)); break;
			case OperandKind::Var:   cmd("mov", reg + ", " + mem(varOffset.at(o.decl)), o.decl->token.text); break;
			case OperandKind::Ref:   cmd("mov", reg + ", " + mem(tempOffset.at(o.value)), "^" + to_string(o.value)); break;
			default: break;
			}
		}

		// Привести eax к типу: int — 16 бит со знаком, word — без знака, bool и так 0 или 1
		void normalize(TokenCode type) {
			if (type == TokenCode::kwInt)  cmd("movsx", "eax, ax", "к int");
			if (type == TokenCode::kwWord) cmd("movzx", "eax, ax", "к word");
		}

		// Сохранить eax во временную ячейку триады number
		void storeResult(int number) {
			cmd("mov", mem(tempOffset.at(number)) + ", eax");
		}

		// Функция — триады с номерами begin+1 .. end (индексы begin .. end-1)
		void function(size_t begin, size_t end) {
			const Node* fn = code[begin].a.decl;
			returnType = fn->type;
			varOffset.clear();
			tempOffset.clear();

			// параметры: первый аргумент положен в стек первым, поэтому он дальше всех от ebp
			vector<const Node*> params;
			for (const auto& c : fn->children)
				if (c->kind == NodeKind::Param) params.push_back(c.get());
			for (size_t i = 0; i < params.size(); ++i)
				varOffset[params[i]] = 8 + 4 * (int)(params.size() - 1 - i);

			// локальные переменные и временные ячейки — ниже ebp
			int frame = 0;
			for (size_t i = begin; i < end; ++i) {
				for (const Operand* o : { &code[i].a, &code[i].b })
					if (o->kind == OperandKind::Var && !varOffset.count(o->decl))
						varOffset[o->decl] = -(frame += 4);
				if (hasResult(code[i].op))
					tempOffset[(int)i + 1] = -(frame += 4);
			}

			out << "\n; ---------------------------------------------------------------\n"
			    << "; функция " << fn->token.text << "\n"
			    << "; ---------------------------------------------------------------\n"
			    << funcLabel(fn) << ":\n";
			cmd("push", "ebp");
			cmd("mov", "ebp, esp");
			if (frame > 0) cmd("sub", "esp, " + to_string(frame), "место под переменные");

			// аргумент мог прийти другого числового типа — привести к типу параметра
			for (const Node* p : params) {
				if (p->type == TokenCode::kwBool) continue;
				cmd("mov", "eax, " + mem(varOffset[p]), p->token.text);
				normalize(p->type);
				cmd("mov", mem(varOffset[p]) + ", eax");
			}

			for (size_t i = begin + 1; i < end; ++i)
				triad((int)i + 1);
		}

		// Одна триада → команды
		void triad(int number) {
			const Triad& t = code[number - 1];
			if (targets.count(number)) out << label(number) << ":\n";
			out << "    ; " << triadText(t, number) << "\n";

			switch (t.op) {
			case TriadOp::Assign:
				load("eax", t.b);
				normalize(t.type);
				cmd("mov", mem(varOffset.at(t.a.decl)) + ", eax", t.a.decl->token.text);
				break;

			case TriadOp::Add: case TriadOp::Sub: case TriadOp::Mul:
			case TriadOp::And: case TriadOp::Or: {
				const char* op = t.op == TriadOp::Add ? "add" : t.op == TriadOp::Sub ? "sub"
				               : t.op == TriadOp::Mul ? "imul" : t.op == TriadOp::And ? "and" : "or";
				load("eax", t.a);
				load("ecx", t.b);
				cmd(op, "eax, ecx");
				normalize(t.type);
				storeResult(number);
				break;
			}

			case TriadOp::Div: case TriadOp::Mod:
				load("eax", t.a);
				load("ecx", t.b);
				cmd("cdq", "", "edx:eax — делимое со знаком");
				cmd("idiv", "ecx", "eax = частное, edx = остаток");
				if (t.op == TriadOp::Mod) cmd("mov", "eax, edx");
				normalize(t.type);
				storeResult(number);
				break;

			case TriadOp::Shl: case TriadOp::Shr:
				load("eax", t.a);
				load("ecx", t.b);
				cmd(t.op == TriadOp::Shl ? "shl" : "sar", "eax, cl");
				normalize(t.type);
				storeResult(number);
				break;

			case TriadOp::Lt: case TriadOp::Gt: case TriadOp::Le:
			case TriadOp::Ge: case TriadOp::Eq: case TriadOp::Ne:
				load("eax", t.a);
				load("ecx", t.b);
				cmd("cmp", "eax, ecx");
				cmd(setcc.at(t.op), "al", "al = 1, если условие верно");
				cmd("movzx", "eax, al");
				storeResult(number);
				break;

			case TriadOp::Not:
				load("eax", t.a);
				cmd("xor", "eax, 1");
				storeResult(number);
				break;

			case TriadOp::Neg:
				load("eax", t.a);
				cmd("neg", "eax");
				normalize(t.type);
				storeResult(number);
				break;

			case TriadOp::Param:
				load("eax", t.a);
				cmd("push", "eax", "аргумент");
				break;

			case TriadOp::Call:
				cmd("call", funcLabel(t.a.decl));
				if (t.b.value > 0) cmd("add", "esp, " + to_string(4 * t.b.value), "убрать аргументы из стека");
				storeResult(number);
				break;

			case TriadOp::Print:
				load("eax", t.a);
				cmd("call", t.type == TokenCode::kwBool ? "rt_print_bool" : "rt_print_int");
				break;

			case TriadOp::Ret:
				load("eax", t.a);
				normalize(returnType);
				cmd("leave", "", "восстановить esp и ebp");
				cmd("ret");
				break;

			case TriadOp::Jmp:
				cmd("jmp", label(t.a.value));
				break;

			case TriadOp::Jf:
				load("eax", t.a);
				cmd("test", "eax, eax");
				cmd("jz", label(t.b.value), "условие ложно");
				break;

			default:                       // C — свёрнута, на неё никто не ссылается
				break;
			}
		}
	};
}

string generateAsm(const vector<Triad>& code) {
	return Generator(code).run();
}
