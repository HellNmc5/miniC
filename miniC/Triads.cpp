#include "Triads.h"
#include <iostream>
#include <unordered_map>

using namespace std;

namespace {
	// Как операции выглядят в выводе — по порядку перечисления TriadOp
	const char* const opNames[] = {
		"proc", "ret", ":=",
		"+", "-", "*", "/", "%", "<<", ">>",
		"<", ">", "<=", ">=", "==", "!=",
		"&&", "||", "!", "neg",
		"param", "call", "jmp", "jf", "C"
	};
    /**
     * @brief 
     * Используется для формирования списка вида:
     * 7: + g, 5
     * 8: * n, ^7.
     * ^7 - ссылка на 7-ой пункт
     * @param o - Операнд
     */
	string operandText(const Operand& o) {
		switch (o.kind) {
		case OperandKind::Const:  return to_string(o.value);
		case OperandKind::Var:
		case OperandKind::Func:   return o.decl->token.text;
		case OperandKind::Ref:    return "^" + to_string(o.value);
		case OperandKind::Target: return to_string(o.value);
		default:                  return "";
		}
	}
    // Короткие способы создать операнд нужного вида
	Operand constant(int v)        { Operand o; o.kind = OperandKind::Const;  o.value = v; return o; }
	Operand ref(int number)        { Operand o; o.kind = OperandKind::Ref;    o.value = number; return o; }
	Operand target(int number)     { Operand o; o.kind = OperandKind::Target; o.value = number; return o; }
	Operand var(const Node* decl)  { Operand o; o.kind = OperandKind::Var;    o.decl = decl; return o; }
	Operand func(const Node* decl) { Operand o; o.kind = OperandKind::Func;   o.decl = decl; return o; }

	// Двухместная операция в дереве → операция триады
	const unordered_map<TokenCode, TriadOp> binaryOps = {
		{ TokenCode::OpPlus, TriadOp::Add },      { TokenCode::OpMinus, TriadOp::Sub },
		{ TokenCode::OpMult, TriadOp::Mul },      { TokenCode::OpDiv, TriadOp::Div },
		{ TokenCode::OpDivPerc, TriadOp::Mod },
		{ TokenCode::OpMoveLeft, TriadOp::Shl },  { TokenCode::OpMoveRight, TriadOp::Shr },
		{ TokenCode::OpMenee, TriadOp::Lt },      { TokenCode::OpBolee, TriadOp::Gt },
		{ TokenCode::OpMenAssi, TriadOp::Le },    { TokenCode::OpBolAssi, TriadOp::Ge },
		{ TokenCode::OpEq, TriadOp::Eq },         { TokenCode::OpNotAssign, TriadOp::Ne },
		{ TokenCode::OpLogAnd, TriadOp::And },    { TokenCode::OpLogOr, TriadOp::Or },
	};
}

string triadText(const Triad& t, int number){
    string s = to_string(number) + ": " + opNames[static_cast<int>(t.op)];
    string a = operandText(t.a), b = operandText(t.b);
    if(!a.empty()) s+=" " + a;
    if(!b.empty()) s+=", " + b;
    return s;
}

void printTriads(const vector<Triad>& code){
    for (size_t i = 0; i<code.size(); ++i){
        cout<<triadText(code[i], (int) i+1)<<endl;
    }
}
//Дописать триаду в конец списка; номер - с единицы
int TriadBuilder::emit(TriadOp op, Operand a, Operand b, TokenCode type){
    code.push_back({op, a, b, type});
    return (int) code.size();
}
/**
 * @brief 
 * 
 * @param number 
 * @param to 
 */
void TriadBuilder::patch(int number, int to){
    Triad& t = code[number-1];
    (t.op == TriadOp::Jmp ? t.a : t.b) = target(to);
}
/**
 * @brief 
 * 
 * @param program 
 * @return vector<Triad> 
 */
vector<Triad> TriadBuilder::build(const Node& program){
    code.clear();
    for(const auto& fn:program.children){
        getFunction(*fn);
    }
    return code;
}
/**
 * @brief 
 * 
 * @param fn 
 */
void TriadBuilder::getFunction(const Node& fn){
    emit(TriadOp::Proc, func(&fn));
    getStmt(*fn.children.back());//Тело - последний ребенок
    emit(TriadOp::Ret,constant(0));//Страховка
}
/**
 * @brief 
 * 
 * Создаем операнды для триад
 * 
 * @param n - Узел
 * @return Operand - то, на что в следующих триадах можно будет сослаться на результат
 */
Operand TriadBuilder::getExpr(const Node& n){
    switch (n.kind){
        case NodeKind::Const:
            return constant(n.token.value);
        case NodeKind::Var:
            return var(n.decl);
        case NodeKind::Unary:{
            Operand a = getExpr(*n.children[0]);
            TriadOp op = n.token.code == TokenCode::OpMinus ? TriadOp::Neg : TriadOp::Not;
            return ref(emit(op,a,{},n.type));
        }
        case NodeKind::Binary:{
            Operand a = getExpr(*n.children[0]);
            Operand b = getExpr(*n.children[1]);
            return ref(emit(binaryOps.at(n.token.code),a,b,n.type));
        }
        case NodeKind::Call:{
            for(const auto& arg: n.children){//Вычисляем каждый аргумент
                emit(TriadOp::Param,getExpr(*arg));
            }
            return ref(emit(TriadOp::Call,func(n.decl), constant((int)n.children.size())));
        }
        default:
            return {};
    }
}

void TriadBuilder::getStmt(const Node& n){
    switch(n.kind){
        case NodeKind::Block:{
            for(const auto& s: n.children){
                getStmt(*s);
            }
            break;
        }
        case NodeKind::VarDecl:{
            if(!n.children.empty()){
                emit(TriadOp::Assign,var(&n), getExpr(*n.children[0]), n.type);
            }
            break;
        }
        case NodeKind::Assign:{
            emit(TriadOp::Assign, var(n.decl), getExpr(*n.children[0]),n.decl->type);
            break;
        }
        case NodeKind::Call:{
            getExpr(n);
            break;
        }
        case NodeKind::Return:{
            emit(TriadOp::Ret,getExpr(*n.children[0]));
            break;
        }
        case NodeKind::If:{
            Operand cond = getExpr(*n.children[0]);
            int jf = emit(TriadOp::Jf, cond);         // «если ложь — прыгнуть…» — куда, пока неизвестно
            getStmt(*n.children[1]);                  // ветка «то»
            if (n.children.size() > 2) {
                int jmp = emit(TriadOp::Jmp);         // после «то» — перепрыгнуть «иначе»
                patch(jf, next());                    // ложь ведёт сюда — в начало «иначе»
                getStmt(*n.children[2]);
                patch(jmp, next());                   // а «то» — сюда, за конец «иначе»
            } else {
                patch(jf, next());                    // без «иначе» ложь ведёт за конец «то»
            }
            break;
        }
        case NodeKind::While:{
            int start = next();
            Operand cond = getExpr(*n.children[0]);
            int jf = emit(TriadOp::Jf, cond);         // условие ложно — выйти из цикла
            getStmt(*n.children[1]);                  // тело
            emit(TriadOp::Jmp, target(start));        // и снова к проверке
            patch(jf, next());                        // выход — сюда, за цикл
            break;
        }
        default:
            break;
    }
}