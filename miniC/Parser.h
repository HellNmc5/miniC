#pragma once
#include <string>
#include <vector>
#include "Ast.h"     // Node, NodePtr, makeNode — из чего строится дерево
#include "Lexer.h"   // Lexer, Token, TokenCode — откуда берутся лексемы (ЛР 1)

/**
 * @brief Синтаксическая ошибка.
 *
 * Создаётся в Parser::error() по текущей лексеме cur:
 *   message — «Ожидалось <что>, а встретилось «<что было>»»;
 *   line, col — строка и позиция лексемы (их записал лексер в Token).
 * Путь: error() ─throw─► catch в parseBlock / parseProgram ─► report() ─► errorList
 *       ─► Parser::errors() ─► main.cpp (печать) и gui.cpp (вкладка «Ошибки»).
 */
struct SyntaxError
{
	std::string message;
	int line, col;
};

/**
 * @brief Синтаксический анализатор miniC — рекурсивный спуск по грамматике LL(1).
 *
 * Использование (main.cpp, gui.cpp):
 *   Parser parser(source);                  // source ─► Lexer, первая лексема ─► cur
 *   NodePtr tree = parser.parseProgram();   // дерево; исключения наружу не выходят
 *   if (!parser.errors().empty()) { ... }   // все найденные ошибки
 */
class Parser
{
public:
	explicit Parser(const std::string& source);

	/*
	 Разбор всей программы (начальный символ <программа>). Возвращает корень Program.
	 Если были ошибки, дерево неполное: в нём нет операторов, которые не удалось разобрать.
	*/
	NodePtr parseProgram();

	// Все синтаксические ошибки в порядке появления.
	const std::vector<SyntaxError>& errors() const { return errorList; }

private:
	Lexer lexer;                        // лексический анализатор ЛР 1; лексемы берёт advance()
	Token cur;                          // текущая, ещё не разобранная лексема — по ней выбираются ветки
	std::vector<SyntaxError> errorList; // накопленные ошибки (заполняет report)

	// --- восстановление после ошибок ---
	void report(const SyntaxError& e);  // ошибка ─► errorList (повтор на том же месте не пишем)
	void syncStatement();               // пропустить лексемы до конца оператора (из parseBlock)
	void syncFunction();                // пропустить лексемы до следующей функции (из parseProgram)

	// --- базовые операции над cur ---
	void advance();                                         // cur = lexer.nextToken()
	bool check(TokenCode code) const;                       // cur такого вида? (не забирает)
	bool checkAny(const std::vector<TokenCode>& codes) const;
	bool match(TokenCode code);                             // совпала — забрать и true
	Token expect(TokenCode code, const std::string& what);  // обязана совпасть, иначе error()
	TokenCode expectType(const std::string& what);          // <тип>: код типа ─► поле type узла

	/*
	 [[noreturn]] — функция всегда бросает исключение и не возвращается;
	 без пометки компилятор предупреждает «не все пути возвращают значение».
	*/
	[[noreturn]] void error(const std::string& message);

	// --- функции разбора: одна на правило РБНФ, каждая возвращает свой узел ---
	NodePtr parseFunction();            // <функция>    ─► Function
	NodePtr parseParam();               // <параметр>   ─► Param
	NodePtr parseBlock();               // <блок>       ─► Block (точка восстановления)
	NodePtr parseStatement();           // <оператор>   ─► выбор ветки по cur
	NodePtr parseVarDecl();             // <объявление> ─► VarDecl
	NodePtr parseIf();                  // <условный>   ─► If
	NodePtr parseWhile();               // <цикл>       ─► While
	NodePtr parseReturn();              // <возврат>    ─► Return
	void parseArguments(Node& call);    // <аргументы>  ─► дописывает детей в узел Call

	NodePtr parseExpression();          // <выражение>  ─► parseBinary(0)
	NodePtr parseBinary(size_t level);  // <или> … <произведение> ─► Binary
	NodePtr parseUnary();               // <унарное>    ─► Unary
	NodePtr parsePrimary();             // <первичное>  ─► Const / Var / Call

	static NodePtr makeBinary(const Token& op, NodePtr left, NodePtr right);  // узел Binary из знака и двух операндов
};
