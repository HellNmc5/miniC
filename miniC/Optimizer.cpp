#include "Optimizer.h"
#include <cstdint>

using namespace std;

namespace{
    /**
     * @brief Обрезать значение до разрядности типа — так, как его получил бы процессор.
	 *        int — 16 бит со знаком, word — 16 бит без знака, bool — 0 или 1.
     * 
     */
    int wrap(int v, TokenCode type){
        if(type==TokenCode::kwInt) return static_cast<int16_t>(v);
        if(type==TokenCode::kwWord) return static_cast<uint16_t>(v);
        return v != 0? 1:0;
    }
    /**
     * @brief Если операнд ссылается на свёрнутую триаду — заменить ссылку её значением
     * 
     */
    void substitute(Operand& o, const vector<Triad>& code){
        if(o.kind != OperandKind::Ref) return;
        const Triad& t = code[o.value-1];
        if (t.op == TriadOp::C){
            o.kind = OperandKind::Const;
            o.value = t.a.value;
        }
    }

    /**
	* @brief Вычислить операцию над двумя числами.
	* @return false, если эта операция не сворачивается или вычислять нельзя (деление на 0)
	*/
	bool evaluate(TriadOp op, int x, int y, int& out) {
		switch (op) {
		case TriadOp::Add: out = x + y; return true;
		case TriadOp::Sub: out = x - y; return true;
		case TriadOp::Mul: out = x * y; return true;
		case TriadOp::Div: if (y == 0) return false; out = x / y; return true;
		case TriadOp::Mod: if (y == 0) return false; out = x % y; return true;
		case TriadOp::Shl: out = y >= 16 ? 0 : x << y; return true;
		case TriadOp::Shr: out = y >= 16 ? 0 : x >> y; return true;
		case TriadOp::Lt:  out = x <  y; return true;
		case TriadOp::Gt:  out = x >  y; return true;
		case TriadOp::Le:  out = x <= y; return true;
		case TriadOp::Ge:  out = x >= y; return true;
		case TriadOp::Eq:  out = x == y; return true;
		case TriadOp::Ne:  out = x != y; return true;
		case TriadOp::And: out = x && y; return true;
		case TriadOp::Or:  out = x || y; return true;
		case TriadOp::Neg: out = -x;     return true;
		case TriadOp::Not: out = !x;     return true;
		default:           return false;      // присваивание, переходы, вызовы — не вычисления
		}
	}
}
/**
 * @brief Свёртка констант — оптимизация по варианту.
 *
 * Если у триады все операнды — числа, её результат известен ещё при компиляции
 * и не зависит от запуска программы. Такая триада вычисляется здесь же
 * и заменяется на «C значение», а процессору остаётся готовое число.
 *
 * Один проход сверху вниз, у каждой триады два шага:
 *   1. подстановка — ссылки ^n на уже свёрнутые триады заменяются их значениями;
 *   2. свёртка — если после подстановки все операнды числа, триада вычисляется.
 *
 * @param code список триад; изменяется на месте
 * @return сколько триад свёрнуто
 */
int foldConstants(vector<Triad>& code){
    int folded = 0;
    for (Triad& t: code){
        // 1. подставить значения свёрнутых триад вместо ссылок на них
        substitute(t.a,code);
        substitute(t.b, code);
        // 2. если все операнды — числа, вычислить и свернуть
        const bool unary = t.op == TriadOp::Neg || t.op == TriadOp::Not;
        const bool aConst = t.a.kind == OperandKind::Const;
        const bool bConst = unary || t.b.kind == OperandKind::Const;// у унарных второго операнда нет

        int value = 0;
        if (aConst && bConst && evaluate(t.op, t.a.value, t.b.value, value)){
            t.op = TriadOp::C;
            t.a.value = wrap(value,t.type);// a уже Const — меняем только значение
            t.b = Operand{};
            ++folded;
        }
    }
    return folded;
}