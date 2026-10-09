#include "Semantic.h"
#include <string>
#include <vector>
/*
 * Symbol* p - указатель: переменная, в которой хранится адрес.
 * Symbol& r - ссылка: переменная, которая ссылается на другой объект.
 * *p - разыменование указателя: получить объект, на который указывает p.
 * &r - получить адрес объекта, получается указатель на объект, на который ссылается r.
 */

using namespace std;
/*
 * Добавить новую область видимости в стек областей.
 */
void Semantic::pushScope(){
    scopes.emplace_back();
}
/*
 * Удалить текущую область видимости из стека областей.
 */
void Semantic::popScope(){
    scopes.pop_back();
}
/*
 * Объявить символ в текущей области видимости. А также проверить, что такого символа ещё нет в текущей области. 
 * Если есть — добавить ошибку.
 * scope - текущая область видимости (верх стека scopes). Содержит пары имя-символ. Вида: std::unordered_map<std::string, Symbol>
 * name - токен с именем символа
 * sym - символ, который нужно добавить в область видимости
 */
void Semantic::declare(const Token& name, const Symbol& sym){
    auto &scope = scopes.back();
    if (scope.find(name.text) != scope.end()) {
        error(name, "Повторное объявление «" + name.text + "»");
    } else {
        scope[name.text] = sym;
    }
}
/*
 * Найти имя в областях видимости — от внутренней к внешней.
 * Возвращает указатель на символ ближайшего объявления или nullptr, если имени нет нигде.
 * find возвращает end(), если в таблице такого имени нет; != end() — значит, нашли.
 * &t->second — адрес найденного символа, то есть указатель на него.
 * Указатель, а не ссылка, — потому что «не нашли» выражается через nullptr.
 */
const Symbol* Semantic::lookup(const std::string& name) const {
    for(size_t i = scopes.size(); i-- >0;){
        auto t =scopes[i].find(name);
        if(t != scopes[i].end()){
            return &t->second;
        }
    }
    return nullptr;
}
/*
 * Добавить ошибку в список ошибок.
 * at - токен, где произошла ошибка
 * message - сообщение об ошибке
 */
void Semantic::error(const Token& at, const string& message){
    errors.push_back({message,at.line,at.colStart});
}

/**
 * @brief Проверить, является ли тип числовым
 * 
 * @param t - тип токена
 */
bool Semantic::isNumeric(TokenCode t){
    return t == TokenCode::kwInt || t == TokenCode::kwWord;
}
/**
 * Проверить совместимость типов
 *
 * @param target - тип переменной, в которую присваиваем
 * @param value - тип значения, которое присваиваем
 */
bool Semantic::compatible(TokenCode target, TokenCode value){
    return (isNumeric(target) && isNumeric(value)) ||
        (target == TokenCode::kwBool && value == TokenCode::kwBool);
}
/**
 * @brief Получить имя типа
 * @param t - тип токена
 * @return имя типа
 */
string Semantic::typeName(TokenCode t){
    switch(t){
        case TokenCode::kwInt: return "int";
        case TokenCode::kwWord: return "word";
        case TokenCode::kwBool: return "bool";
        default: return "unknown";
    }
}
/**
 * @brief Проверить выражение и вернуть его тип
 * @param n - узел выражения, состоящий из токена и дочерних узлов
 * @return тип выражения
 */
TokenCode Semantic::checkExpression(Node& n){
    const TokenCode unknown{};
    TokenCode result = unknown;
    switch(n.kind){
        // Константа: тип зависит от токена. IntConst — int или word, BoolTrue/False — bool.
        case NodeKind::Const:{
            if(n.token.code == TokenCode::IntConst)
                result = n.token.value <= 32767 ? TokenCode::kwInt : TokenCode::kwWord;
            else
                result = TokenCode::kwBool;
            break;
        }
        // Переменная: ищем её в областях видимости. Если нашли, берём её тип. Если не нашли — ошибка.
        case NodeKind::Var:{
            const Symbol* s = lookup(n.token.text);
            if(!s)
                error(n.token, "переменная «" + n.token.text + "» не объявлена");
            else if (s->kind == NodeKind::Function)
                error(n.token, "«" + n.token.text + "» - функция, а не переменная");
            else
                result = s->type;
            break;
        }
        /* Унарная операция */
        case NodeKind::Unary:{
            TokenCode operandType = checkExpression(*n.children[0]);// проверить тип операнда. operandType = тип операнда
            
            if(operandType == unknown)
                break;
            if(n.token.code == TokenCode::OpMinus){
                if(isNumeric(operandType))// если тип операнда числовой, то результат такой же
                    result = TokenCode::kwInt;//result = тип операнда: int
                else
                    error(n.token, "операция «-» применима только к числовым типам");
            }
            else{// n.token.code == TokenCode::OpLogNot
                if(operandType == TokenCode::kwBool)
                    result = TokenCode::kwBool;//result = тип результата: bool
                else
                    error(n.token, "операция «!» применима только к типу bool");
            }
            break;
        }
        // Двухместная операция: проверяем типы левого и правого операндов. 
        // Если они совместимы с операцией, то результат зависит от операции.
        case NodeKind::Binary:{
            TokenCode l = checkExpression(*n.children[0]);// проверить тип левого операнда
            TokenCode r = checkExpression(*n.children[1]);// проверить тип правого операнда
            if(l == unknown || r == unknown)
                break;
            
            TokenCode op = n.token.code;
            bool ok = false;

            if(op == TokenCode::OpPlus || op == TokenCode::OpMinus ||
               op == TokenCode::OpMult || op == TokenCode::OpDiv || op == TokenCode::OpDivPerc){ // + - * / %
                ok = isNumeric(l) && isNumeric(r);// проверка, что оба операнда числовые
                result = (l == TokenCode::kwWord || r == TokenCode::kwWord) ? TokenCode::kwWord : TokenCode::kwInt;
            }
            else if (op == TokenCode::OpMoveLeft || op == TokenCode::OpMoveRight){// << >>
                ok = isNumeric(l) && isNumeric(r);// проверка, что оба операнда числовые
                result = l; // результат имеет тип левого операнда
            }
            else if (op == TokenCode::OpEq || op == TokenCode::OpNotAssign){// == !=
                ok = compatible(l,r);// проверка совместимости типов
                result = TokenCode::kwBool;
            }
            else if (op == TokenCode::OpLogAnd || op == TokenCode::OpLogOr){// && ||
                ok = l == TokenCode::kwBool && r == TokenCode::kwBool;
                result = TokenCode::kwBool;
            }
            else { // < > <= >=
                ok = isNumeric(l) && isNumeric(r);
                result = TokenCode::kwBool;
            }
            if(!ok){
                error(n.token, "операция «" + n.token.text + "» неприменима к " + typeName(l) + " и " + typeName(r));
                result = unknown;
            }
            break;
        }
        // Вызов функции: ищем функцию в областях видимости. Если нашли, проверяем количество и типы аргументов.
        case NodeKind::Call:{
            const Symbol* s = lookup(n.token.text);
            if(!s)
                error(n.token, "функция «" + n.token.text + "» не объявлена");
            else if(s->kind != NodeKind::Function){
                error(n.token, "«" + n.token.text + "» не является функцией");
                s = nullptr;
            }

            vector<TokenCode> params;
            if(s){
                //s->node — это узел объявления функции. Его children — Среди его детей — параметры и последним блок; берём только параметры.
                // Перебираем их и добавляем их типы в params.
                for(const auto& child : s->node->children){// перебираем параметры функции
                    if(child->kind == NodeKind::Param)
                        params.push_back(child->type);// добавляем тип параметра в список
                }
            }

            for (size_t i = 0; i < n.children.size(); ++i){
                TokenCode a = checkExpression(*n.children[i]);// проверяем тип аргумента
                if(s && i < params.size() && a != unknown && !compatible(params[i], a)){// проверяем совместимость 
                    // типов параметра и аргумента
                    error(n.children[i]->token, "аргумент " + std::to_string(i + 1) + " функции «" + n.token.text +
				      "»: ожидался " + typeName(params[i]) + ", передан " + typeName(a));
                }
            }

            if(s){
                if(n.children.size() != params.size()){// проверяем количество аргументов
                    error(n.token, "функция «" + n.token.text + "» ожидает аргументов: " + std::to_string(params.size()) +
				      ", передано: " + std::to_string(n.children.size()));
                }
                result = s->type;// результат имеет тип, который возвращает функция
            }
            break;
        }
        default:
            break;
        }
        n.type = result;
        return result;
}
/*
 * Проверить программу и вернуть список семантических ошибок.
 * program - корень дерева разбора программы
 */
vector<SemanticError> Semantic::check(Node& program){
    errors.clear();
    scopes.clear();
    pushScope();// глобальная область — здесь живут функции
    // 1. все функции регистрируются заранее, чтобы main могла вызвать функцию, объявленную ниже
    for(auto& fn : program.children){
        declare(fn->token, {NodeKind::Function, fn->type, fn.get()});
    }
    // 2. проверка тел функций: области видимости, типы, возвраты, аргументы
    for(auto& fn : program.children){
        checkFunction(*fn);
    }
    // 3. проверка, что функция main объявлена
    if(!lookup("main")){
        error(program.token, "функция main не объявлена");
    }
    popScope();
    return errors;
}
/*
 * Проверить функцию: области видимости, типы, возвраты, аргументы
 * fn - узел функции
 */
void Semantic::checkFunction(Node& fn){
    currentReturn = fn.type;// для проверки return внутри
    pushScope();// область параметров
    for (auto& child : fn.children){
        if(child->kind == NodeKind::Param){
            declare(child->token, {NodeKind::Param, child->type, child.get()});
        }
    }
    checkStatement(*fn.children.back());// тело — последний ребёнок, блок
    popScope();
}

/*
 * Проверить оператор. В отличие от checkExpression ничего не возвращает —
 * только проверяет и наполняет области видимости: объявления добавляют имена,
 * блоки открывают и закрывают области.
 */
void Semantic::checkStatement(Node& n) {
    const TokenCode unknown{};

    switch (n.kind) {

    // Блок: своя область видимости — всё, что объявлено внутри, исчезнет на выходе
    case NodeKind::Block: {
        pushScope();
        for (auto& child : n.children)
            checkStatement(*child);
        popScope();
        break;
    }

    // Объявление: сначала значение, потом имя — чтобы в «int x = x + 1;» x ещё не была видна
    case NodeKind::VarDecl: {
        if (!n.children.empty()) {
            TokenCode t = checkExpression(*n.children[0]);
            if (t != unknown && !compatible(n.type, t))
                error(n.token, "нельзя присвоить " + typeName(t) + " переменной типа " + typeName(n.type));
        }
        declare(n.token, { NodeKind::VarDecl, n.type, &n });
        break;
    }

    // Присваивание: переменная должна существовать и принимать значение такого типа
    case NodeKind::Assign: {
        const Symbol* s = lookup(n.token.text);
        if (!s) {
            error(n.token, "переменная «" + n.token.text + "» не объявлена");
        } else if (s->kind == NodeKind::Function) {
            error(n.token, "нельзя присвоить значение функции «" + n.token.text + "»");
            s = nullptr;
        }

        TokenCode t = checkExpression(*n.children[0]);    // правую часть проверяем в любом случае
        if (s && t != unknown && !compatible(s->type, t))
            error(n.token, "нельзя присвоить " + typeName(t) + " переменной типа " + typeName(s->type));
        break;
    }

    // Вызов как оператор: все проверки уже есть в checkExpression, результат не нужен
    case NodeKind::Call:
        checkExpression(n);
        break;

    // Условие: условие обязано быть bool; ветка «иначе» — третий ребёнок, если есть
    case NodeKind::If: {
        TokenCode cond = checkExpression(*n.children[0]);
        if (cond != unknown && cond != TokenCode::kwBool)
            error(n.children[0]->token, "условие if должно быть bool, а получено " + typeName(cond));
        checkStatement(*n.children[1]);
        if (n.children.size() > 2)
            checkStatement(*n.children[2]);
        break;
    }

    // Цикл: как условие, без «иначе»
    case NodeKind::While: {
        TokenCode cond = checkExpression(*n.children[0]);
        if (cond != unknown && cond != TokenCode::kwBool)
            error(n.children[0]->token, "условие while должно быть bool, а получено " + typeName(cond));
        checkStatement(*n.children[1]);
        break;
    }

    // Возврат: тип выражения должен подходить к типу результата текущей функции
    case NodeKind::Return: {
        TokenCode t = checkExpression(*n.children[0]);
        if (t != unknown && !compatible(currentReturn, t))
            error(n.token, "функция возвращает " + typeName(currentReturn) + ", а return — " + typeName(t));
        break;
    }

    default:
        break;
    }
}
