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
    /**
     * @brief Создать операнд-константу.
     * 
     * @param v 
     */
	Operand constant(int v)        { Operand o; o.kind = OperandKind::Const;  o.value = v; return o; }
    /**
     * @brief Создать ссылку на результат другой триады — в выводе «^number».
     * 
     * @param number номер триады, с единицы
     */
	Operand ref(int number)        { Operand o; o.kind = OperandKind::Ref;    o.value = number; return o; }
    /**
     * @brief Создать цель перехода для jmp и jf.
     * 
     * @param number номер триады, к которой перейти
     */
	Operand target(int number)     { Operand o; o.kind = OperandKind::Target; o.value = number; return o; }
    /**
     * @brief Создать операнд-переменную.
     * 
     * @param decl узел объявления — VarDecl или Param; по нему генератор найдёт,
     * где переменная лежит в памяти, даже если имена совпадают
     */
	Operand var(const Node* decl)  { Operand o; o.kind = OperandKind::Var;    o.decl = decl; return o; }
    /**
     * @brief Создать операнд-функцию для триад proc и call.
     * 
     * @param decl узел объявления функции
     */
	Operand func(const Node* decl) { Operand o; o.kind = OperandKind::Func;   o.decl = decl; return o; }

	// Двухместная операция в дереве → операция триады
    /**
    * Соответствие двухместных операций дерева операциям триад.
    * В дереве операция хранится кодом лексемы (OpPlus), в триаде — своим кодом (Add).
    */
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
/**
 * @brief Одна триада в виде строки, например «8: *  n, ^7».
 * @param t      триада
 * @param number её номер в списке, с единицы
 * @return строка для вывода в консоль, интерфейс или записку
 */
string triadText(const Triad& t, int number){
    string s = to_string(number) + ": " + opNames[static_cast<int>(t.op)];
    string a = operandText(t.a), b = operandText(t.b);
    if(!a.empty()) s+=" " + a;
    if(!b.empty()) s+=", " + b;
    return s;
}
/**
 * @brief Вывести список триад в консоль, по одной в строке.
 */
void printTriads(const vector<Triad>& code){
    for (size_t i = 0; i<code.size(); ++i){
        cout<<triadText(code[i], (int) i+1)<<endl;
    }
}

/**
 * @brief Дописать триаду в конец списка.
 * @param op   операция
 * @param a    первый операнд, если есть
 * @param b    второй операнд, если есть
 * @param type тип результата — int, word или bool; генератору он нужен,
 *             чтобы выбрать знаковую или беззнаковую команду
 * @return номер созданной триады, с единицы — на него потом ссылаются через ^номер
 */
int TriadBuilder::emit(TriadOp op, Operand a, Operand b, TokenCode type){
    code.push_back({op, a, b, type});
    return (int) code.size();
}
/**
 * @brief Вписать цель в уже созданный переход — «обратная заплатка».
 *
 * Когда создаётся переход вперёд, его цель ещё неизвестна: код, через который
 * надо перепрыгнуть, ещё не сгенерирован. Переход создаётся с пустой целью,
 * а когда код готов, цель вписывается этим методом.
 *
 * @param number номер перехода
 * @param to     номер триады, к которой он должен вести
 */
void TriadBuilder::patch(int number, int to){
    Triad& t = code[number-1];
    (t.op == TriadOp::Jmp ? t.a : t.b) = target(to);
}
/**
 * @brief Построить триады для всей программы.
 * @param program корень дерева — узел Program; семантика уже должна быть пройдена
 *                без ошибок, иначе у переменных не будет ссылок на объявления
 * @return триады всех функций одним списком, по порядку объявления функций
 */
vector<Triad> TriadBuilder::build(const Node& program){
    code.clear();
    for(const auto& fn:program.children){
        getFunction(*fn);
    }
    return code;
}
/**
 * @brief Триады одной функции: proc, тело, страховочный ret 0.
 *
 * ret 0 в конце нужен на случай, если выполнение дошло до конца функции
 * без return, и как цель для переходов, которые ведут «за конец» тела.
 */
void TriadBuilder::getFunction(const Node& fn){
    emit(TriadOp::Proc, func(&fn));
    getStmt(*fn.children.back());//Тело - последний ребенок
    emit(TriadOp::Ret,constant(0));//Страховка
}
/**
 * @brief Выражение → триады; вернуть операнд с его результатом.
 *
 * Обход снизу вверх: сначала операнды, потом операция. Константа и
 * переменная — уже готовые операнды, триад для них не создаётся.
 * Для операций и вызовов создаётся триада, и возвращается ссылка на неё.
 *
 * @param n узел выражения
 * @return то, через что на результат можно сослаться в следующих триадах
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
/**
 * @brief Оператор → триады.
 *
 * Простые операторы дают одну-две триады. if и while превращаются
 * в переходы jf и jmp, цели которых вписываются через patch:
 *   if:    условие, jf → «иначе», ветка «то», jmp → конец, ветка «иначе»
 *   while: условие, jf → выход, тело, jmp → обратно к условию
 *
 * @param n узел оператора
 */
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