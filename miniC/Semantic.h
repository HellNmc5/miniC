#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "Ast.h"

// Семантическая ошибка: что не так и где
struct SemanticError {
	std::string message;
	int line, col;
};

/*
 * Символ в таблице символов.
 * kind - вид узла объявления: Function, Param или VarDecl
 * type - тип переменной или тип результата функции
 * node - узел объявления — для функции оттуда берутся параметры
 */
struct Symbol {
	NodeKind kind;             // Function, Param или VarDecl
	TokenCode type;            // тип переменной или тип результата функции
	const Node* node;          // узел объявления — для функции оттуда берутся параметры
};
/*
 * Класс для выполнения семантической проверки программы.
 * Проверка выполняется в один проход по дереву разбора.
 * Задачи:
 * 1. Проверка областей видимости: объявление переменной/функции до использования, повторное объявление.
 * 2. Проверка типов: присваивание, вызов функции, возврат из функции.
 * 3. Проверка возврата из функции: все пути должны возвращать значение.
 * 4. Проверка аргументов функции: количество и типы.
 * 5. Проверка использования переменных: объявление до использования.
 */
class Semantic {
public:
	std::vector<SemanticError> check(Node& program);   // проверить программу, вернуть все ошибки

private:
	std::vector<std::unordered_map<std::string, Symbol>> scopes;   // стек областей видимости
	std::vector<SemanticError> errors;
	TokenCode currentReturn{};                         // тип результата функции, которую сейчас проверяем

	void pushScope();
	void popScope();
	void declare(const Token& name, const Symbol& sym);    // объявить в текущей области; повтор — ошибка
	const Symbol* lookup(const std::string& name) const;   // найти от вершины стека вниз; нет — nullptr

	void error(const Token& at, const std::string& message);

	TokenCode checkExpression(Node& n);   // проверить выражение, вернуть его тип. Пишет его в n.type
	static bool isNumeric(TokenCode t);  // kwInt или kwWord
	static bool compatible(TokenCode target, TokenCode value);  // можно ли присвоить value переменной типа target
	static std::string typeName(TokenCode t);  // имя типа "int", "word" или "bool"
	
	void checkFunction(Node& fn);
	void checkStatement(Node& n);
};