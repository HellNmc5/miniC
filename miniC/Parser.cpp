#include "Parser.h"
#include <vector>

using namespace std;

/*
 * ============================================================================
 *  СИНТАКСИЧЕСКИЙ АНАЛИЗАТОР miniC (ЛР 2) — что откуда приходит и куда уходит
 * ============================================================================
 *
 *  ВХОД:   std::string source — текст программы (из main.cpp или gui.cpp).
 *  ВЫХОД:  NodePtr — корень дерева разбора (возвращает parseProgram)
 *          + список SyntaxError (отдаёт errors()).
 *
 *  Поток данных:
 *
 *    source ──► Lexer (ЛР 1) ──nextToken()──► cur ──► функции разбора ──► узлы Node
 *                                                         │                   │
 *                                                         │ error()           └─► children родителя
 *                                                         ▼                        └─► ... ─► Program
 *                                              throw SyntaxError
 *                                                         │
 *                                     catch в parseBlock / parseProgram
 *                                                         │
 *                                          report() ─► errorList ─► errors()
 *                                          sync...() ─► пропуск лексем ─► разбор дальше
 *
 *  Путь по методам (кто кого вызывает; ↺ — повторный вызов функции выше):
 *
 *    parseProgram                         цикл до конца файла, catch ─► syncFunction
 *    └─ parseFunction
 *       ├─ parseParam                     для каждого параметра
 *       └─ parseBlock                     цикл до «}», catch ─► syncStatement
 *          └─ parseStatement              ветка выбирается по cur:
 *             ├─ parseVarDecl             тип ...
 *             ├─ parseIf                  if ...      ─► parseStatement ↺ (then, else)
 *             ├─ parseWhile               while ...   ─► parseStatement ↺ (тело)
 *             ├─ parseReturn              return ...
 *             ├─ parseBlock ↺             { ...       (вложенный блок)
 *             └─ присваивание / вызов     имя = ...  |  имя ( ... ─► parseArguments
 *
 *    Все ветки, где нужно выражение, приходят в одну точку:
 *    parseExpression
 *    └─ parseBinary(0)   ||                  каждый уровень сначала вызывает
 *       └─ parseBinary(1)   &&               следующий, а потом смотрит на свой знак
 *          └─ ... parseBinary(6)   * / %
 *             └─ parseBinary(7) ─► parseUnary ─► parsePrimary
 *                                     ↺ (-, !)    ├─ ( выражение ) ─► parseExpression ↺
 *                                                 └─ имя ( ... )   ─► parseArguments ─► parseExpression ↺
 *
 *  Как строится дерево: каждая функция parseX разбирает одно правило РБНФ
 *  и ВОЗВРАЩАЕТ готовый узел. Вызвавшая функция кладёт его в children своего
 *  узла. Так из вложенных вызовов складывается дерево снизу вверх.
 *
 *  Пометки в комментариях к функциям:
 *    cur ДО     — какая лексема лежит в cur, когда функцию вызывают;
 *    cur ПОСЛЕ  — какая лексема лежит в cur, когда функция вернулась;
 *    ВОЗВРАТ    — что функция отдаёт и кто это забирает.
 */

namespace {
	/*
	 Таблица уровней приоритета для parseBinary: строка = одно правило РБНФ
	 (<или>, <и>, <равенство>, <сравнение>, <сдвиг>, <сумма>, <произведение>).
	 Чем больше индекс, тем сильнее операция и тем глубже она в дереве.
	*/
	const vector<vector<TokenCode>> levels = {
		{ TokenCode::OpLogOr },                                   // 0: ||
		{ TokenCode::OpLogAnd },                                  // 1: &&
		{ TokenCode::OpEq, TokenCode::OpNotAssign },              // 2: == !=
		{ TokenCode::OpMenee, TokenCode::OpBolee,
		  TokenCode::OpMenAssi, TokenCode::OpBolAssi },           // 3: < > <= >=
		{ TokenCode::OpMoveLeft, TokenCode::OpMoveRight },        // 4: << >>
		{ TokenCode::OpPlus, TokenCode::OpMinus },                // 5: + -
		{ TokenCode::OpMult, TokenCode::OpDiv, TokenCode::OpDivPerc },  // 6: * / %
	};

	// Коды типов: с них начинается функция, параметр и объявление переменной
	const vector<TokenCode> types = { TokenCode::kwInt, TokenCode::kwWord, TokenCode::kwBool };
}

/* ===================== Связь с лексером (интеграция с ЛР 1) ===================== */

/*
 Конструктор: source уходит в конструктор Lexer (поле lexer),
 затем advance() кладёт в cur первую лексему программы.
*/
Parser::Parser(const string& source) : lexer(source) {
	advance();
}

/*
 Единственное место, где парсер получает лексемы от лексера.
 cur ДО: лексема, которую уже разобрали.  cur ПОСЛЕ: следующая лексема.
 Лексемы-ошибки (код Error) уже показаны лексическим анализом, здесь пропускаются.
*/
void Parser::advance() {
	cur = lexer.nextToken();
	while (cur.code == TokenCode::Error) {
		cur = lexer.nextToken();
	}
}

/* ===================== Базовые операции над cur ===================== */

// Только смотрит: cur такого вида? cur не меняется.
bool Parser::check(TokenCode code) const {
	return cur.code == code;
}

// cur одного из перечисленных видов? cur не меняется.
bool Parser::checkAny(const vector<TokenCode>& codes) const {
	for (TokenCode c : codes)
		if (check(c)) return true;
	return false;
}

/*
 Необязательная лексема ( [ ... ] и { ... } в РБНФ ).
 Совпала — забираем (advance) и возвращаем true; нет — cur не трогаем, false.
*/
bool Parser::match(TokenCode code) {
	if (check(code)) {
		advance();
		return true;
	}
	return false;
}

/*
 Обязательная лексема.
 ВОЗВРАТ: забранная лексема — нужна, когда из неё берут имя (parseVarDecl, parseParam).
 Не совпала — error(): исключение уходит вверх до ближайшего catch.
*/
Token Parser::expect(TokenCode code, const string& what) {
	if (check(code)) {
		Token t = cur;
		advance();
		return t;
	}
	error("Ожидалось " + what);
}

/*
 Формирует сообщение по требованиям методички: что ожидалось + что встретилось,
 строка и позиция берутся из cur (лексер записал их в Token).
 throw SyntaxError ─► catch в parseBlock (ошибка в операторе)
                  └─► или catch в parseProgram (ошибка вне блока).
*/
void Parser::error(const string& message) {
	string got = cur.code == TokenCode::EndOfFile ? "конец файла" : "«" + cur.text + "»";
	throw SyntaxError{ message + ", а встретилось " + got, cur.line, cur.colStart };
}

/* ===================== Выражения: parseExpression → parseBinary(0..6) → parseUnary → parsePrimary ===================== */

/*
 Собирает узел Binary. op — лексема знака (станет node->token),
 left и right — уже готовые поддеревья операндов (unique_ptr переносятся через move).
 ВОЗВРАТ: новый узел ─► parseBinary, который сделает его левым операндом следующего знака.
*/
NodePtr Parser::makeBinary(const Token& op, NodePtr left, NodePtr right) {
	NodePtr n = makeNode(NodeKind::Binary, op);
	n->children.push_back(move(left));    // children[0] — левый операнд
	n->children.push_back(move(right));   // children[1] — правый (для «-» порядок важен)
	return n;
}

/*
 <выражение> ::= <или>
 Вызывают: parseVarDecl, присваивание и вызов в parseStatement, parseIf, parseWhile,
           parseReturn, parseArguments, скобки в parsePrimary.
 ВОЗВРАТ: корень поддерева выражения ─► вызывающий кладёт его в children своего узла.
*/
NodePtr Parser::parseExpression() {
	return parseBinary(0);
}

/*
 Одно правило вида  <уровень> ::= <уровень+1> { знак_уровня <уровень+1> }.
 level = индекс строки в таблице levels.
 cur ДО: первая лексема операнда.  cur ПОСЛЕ: первая лексема, которая не является
         знаком этого уровня (её разберёт уровень слабее или вызывающая функция).
 ВОЗВРАТ: поддерево ─► parseBinary(level - 1) или parseExpression.
*/
NodePtr Parser::parseBinary(size_t level) {
	if (level == levels.size()) {                          // уровни кончились —
		return parseUnary();                               //   дальше только операнд
	}
	NodePtr left = parseBinary(level + 1);                 // левый операнд — с уровня сильнее
	while (checkAny(levels[level])) {                      // { знак <операнд> } — цикл вместо левой рекурсии
		Token op = cur;                                    //   запомнили знак
		advance();                                         //   забрали его
		NodePtr right = parseBinary(level + 1);            //   правый операнд — тоже с уровня сильнее
		left = makeBinary(op, move(left), move(right));    //   собранное — левая часть следующей операции
	}                                                      //   (отсюда левая ассоциативность: (a-b)-c)
	return left;
}

/*
 <унарное> ::= ( "-" | "!" ) <унарное> | <первичное>
 ВОЗВРАТ: узел Unary (знак в token, операнд в children[0]) или результат parsePrimary
          ─► parseBinary(6).
*/
NodePtr Parser::parseUnary() {
	if (checkAny({ TokenCode::OpMinus, TokenCode::OpLogNot })) {
		Token op = cur;
		advance();
		NodePtr n = makeNode(NodeKind::Unary, op);
		n->children.push_back(parseUnary());               // рекурсия на себя: !!a = !(!a)
		return n;
	}
	return parsePrimary();
}

/*
 <первичное> ::= константа | true | false
               | идентификатор [ "(" [ <аргументы> ] ")" ]
               | "(" <выражение> ")"
 Ветка выбирается по одной лексеме cur — это и есть LL(1).
 ВОЗВРАТ: Const / Var / Call или узел выражения из скобок ─► parseUnary.
*/
NodePtr Parser::parsePrimary() {
	// 1. константа: лексема целиком уходит в узел (значение — в token.value от лексера)
	if (checkAny({ TokenCode::IntConst, TokenCode::BoolTrue, TokenCode::BoolFalse })) {
		Token t = cur;
		advance();
		return makeNode(NodeKind::Const, t);
	}

	// 2. имя: переменная или вызов — решает следующая лексема
	if (check(TokenCode::Identfier)) {
		Token name = cur;
		advance();
		if (!match(TokenCode::LParent)) {                  // нет «(» — переменная
			return makeNode(NodeKind::Var, name);
		}
		NodePtr call = makeNode(NodeKind::Call, name);     // есть «(» — вызов;
		parseArguments(*call);                             //   аргументы допишутся в call->children
		return call;
	}

	// 3. скобки: разбор заново с уровня 0; отдельного узла для скобок нет
	if (match(TokenCode::LParent)) {
		NodePtr inner = parseExpression();
		expect(TokenCode::RParent, "«)»");
		return inner;
	}

	// 4. ни одна ветка не подошла — «некорректное выражение»
	error("Ожидалось выражение");
}

/*
 <аргументы> ::= <выражение> { "," <выражение> }  и закрывающая «)».
 Узла не возвращает: каждый аргумент сразу кладётся в call.children
 (call передан по ссылке из parsePrimary или parseStatement).
 cur ДО: лексема сразу после «(».  cur ПОСЛЕ: лексема после «)».
*/
void Parser::parseArguments(Node& call) {
	if (!check(TokenCode::RParent)) {                      // сразу «)» — аргументов нет
		call.children.push_back(parseExpression());
		while (match(TokenCode::Comm)) {
			call.children.push_back(parseExpression());
		}
	}
	expect(TokenCode::RParent, "«)»");
}

/* ===================== Операторы ===================== */

/*
 <оператор> ::= <объявление> | <условный> | <цикл> | <возврат> | <блок>
              | идентификатор <продолжение>
 Вызывают: parseBlock (оператор блока), parseIf и parseWhile (тело).
 cur ДО: первая лексема оператора — по ней выбирается функция.
 ВОЗВРАТ: узел оператора ─► parseBlock кладёт его в children блока
          (или parseIf / parseWhile — как ветку / тело).
*/
NodePtr Parser::parseStatement() {
	if (checkAny(types))                return parseVarDecl();
	if (check(TokenCode::kwIf))         return parseIf();
	if (check(TokenCode::kwWhile))      return parseWhile();
	if (check(TokenCode::kwReturn))     return parseReturn();
	if (check(TokenCode::LFigParent))   return parseBlock();

	if (check(TokenCode::Identfier)) {
		Token name = cur;                                  // имя уйдёт в token узла
		advance();
		// <продолжение> ::= "=" <выражение> ";"          — присваивание
		if (match(TokenCode::OpAssign)) {
			NodePtr n = makeNode(NodeKind::Assign, name);
			n->children.push_back(parseExpression());      // children[0] — присваиваемое значение
			expect(TokenCode::DotComm, "«;»");
			return n;
		}
		//                 | "(" [ <аргументы> ] ")" ";"   — вызов как оператор (например, print)
		if (match(TokenCode::LParent)) {
			NodePtr n = makeNode(NodeKind::Call, name);
			parseArguments(*n);
			expect(TokenCode::DotComm, "«;»");
			return n;
		}
		error("Ожидалось «=» или «(»");                    // нарушен порядок лексем
	}

	error("Ожидался оператор");
}

/*
 <условный> ::= "if" "(" <выражение> ")" <оператор> [ "else" <оператор> ]
 ВОЗВРАТ: узел If: children[0] — условие, [1] — then, [2] — else (если есть).
*/
NodePtr Parser::parseIf() {
	NodePtr n = makeNode(NodeKind::If, cur);               // token узла — лексема if (для строки ошибок)
	advance();
	expect(TokenCode::LParent, "«(» после if");
	n->children.push_back(parseExpression());
	expect(TokenCode::RParent, "«)» после условия if");
	n->children.push_back(parseStatement());
	if (match(TokenCode::kwElse)) {                        // ближайший if забирает else первым —
		n->children.push_back(parseStatement());           //   так решён «висячий» else
	}
	return n;
}

/*
 <цикл> ::= "while" "(" <выражение> ")" <оператор>
 ВОЗВРАТ: узел While: children[0] — условие, [1] — тело.
*/
NodePtr Parser::parseWhile() {
	NodePtr n = makeNode(NodeKind::While, cur);
	advance();
	expect(TokenCode::LParent, "«(» после while");
	n->children.push_back(parseExpression());
	expect(TokenCode::RParent, "«)» после условия while");
	n->children.push_back(parseStatement());
	return n;
}

/*
 <возврат> ::= "return" <выражение> ";"
 ВОЗВРАТ: узел Return: children[0] — возвращаемое выражение.
*/
NodePtr Parser::parseReturn() {
	NodePtr n = makeNode(NodeKind::Return, cur);
	advance();
	n->children.push_back(parseExpression());
	expect(TokenCode::DotComm, "«;» после return");
	return n;
}

/*
 <блок> ::= "{" { <оператор> } "}"
 Первая точка восстановления: ошибка внутри оператора ловится здесь,
 поэтому один плохой оператор не обрывает разбор всего блока.
 cur ДО: «{».  cur ПОСЛЕ: лексема после «}».
 ВОЗВРАТ: узел Block со всеми удачно разобранными операторами ─► parseFunction
          (тело функции) или parseStatement (вложенный блок).
*/
NodePtr Parser::parseBlock() {
	NodePtr n = makeNode(NodeKind::Block, cur);
	advance();
	while (!check(TokenCode::RFigParent) && !check(TokenCode::EndOfFile)) {
		try {
			n->children.push_back(parseStatement());       // узел оператора ─► children блока
		}
		catch (const SyntaxError& e) {                     // исключение пришло из error() внутри оператора
			report(e);                                     //   ошибка ─► errorList
			syncStatement();                               //   пропуск до конца оператора, разбор дальше
		}
	}
	expect(TokenCode::RFigParent, "«}» после блока");     // нет «}» — исключение уйдёт в parseProgram
	return n;
}

/*
 <объявление> ::= <тип> идентификатор [ "=" <выражение> ] ";"
 ВОЗВРАТ: узел VarDecl: token — имя, type — код типа, children[0] — начальное значение (если есть).
*/
NodePtr Parser::parseVarDecl() {
	TokenCode type = expectType("переменной");
	Token name = expect(TokenCode::Identfier, "имя переменной");  // expect вернул лексему имени
	NodePtr n = makeNode(NodeKind::VarDecl, name);
	n->type = type;
	if (match(TokenCode::OpAssign)) n->children.push_back(parseExpression());
	expect(TokenCode::DotComm, "«;»");
	return n;
}

/*
 <тип> ::= "int" | "word" | "bool"
 Узла не создаёт. ВОЗВРАТ: код типа ─► поле type узла Function / Param / VarDecl.
*/
TokenCode Parser::expectType(const std::string& what) {
	if (checkAny(types)) {
		TokenCode type = cur.code;
		advance();
		return type;
	}
	error("Ожидался тип " + what);
}

/* ===================== Функции и программа ===================== */

/*
 <функция> ::= <тип> <имя функции> "(" [ <параметры> ] ")" <блок>
 ВОЗВРАТ: узел Function: token — имя, type — тип результата,
          children — сначала Param, последним — Block ─► parseProgram.
*/
NodePtr Parser::parseFunction() {
	TokenCode type = expectType("функции");

	if (!checkAny({ TokenCode::Identfier, TokenCode::kwMain })) {   // main — ключевое слово
		error("Ожидалось имя функции");
	}
	NodePtr n = makeNode(NodeKind::Function, cur);
	n->type = type;
	advance();

	expect(TokenCode::LParent, "«(» после имени функции");
	if (!check(TokenCode::RParent)) {
		n->children.push_back(parseParam());
		while (match(TokenCode::Comm)) {
			n->children.push_back(parseParam());
		}
	}
	expect(TokenCode::RParent, "«)» после списка параметров");

	n->children.push_back(parseBlock());                   // тело — последний ребёнок
	return n;
}

/*
 <параметр> ::= <тип> идентификатор
 ВОЗВРАТ: узел Param (token — имя, type — тип) ─► children функции.
*/
NodePtr Parser::parseParam() {
	TokenCode type = expectType("параметра");
	Token name = expect(TokenCode::Identfier, "имени параметра");
	NodePtr n = makeNode(NodeKind::Param, name);
	n->type = type;
	return n;
}

/*
 <программа> ::= { <функция> }   — начальный символ грамматики.
 Вторая точка восстановления: сюда доходят ошибки в заголовке функции,
 незакрытый блок (нет «}» до конца файла) и лишние лексемы вне функций.
 ВОЗВРАТ: корень Program ─► main.cpp / gui.cpp (printTree, вкладка «Дерево»,
          а при отсутствии ошибок — дальше в семантический анализ).
*/
NodePtr Parser::parseProgram() {
	NodePtr programNode = makeNode(NodeKind::Program, Token{});
	while (!check(TokenCode::EndOfFile)) {
		try {
			if (!checkAny(types)) {                        // на верхнем уровне — только описание функции
				error("Лишняя лексема вне функций: ожидалось описание функции или конец файла");
			}
			programNode->children.push_back(parseFunction());   // узел Function ─► children программы
		}
		catch (const SyntaxError& e) {
			report(e);                                     // ошибка ─► errorList
			syncFunction();                                // пропуск до следующей функции
		}
	}
	return programNode;
}

/* ===================== Восстановление после ошибок (режим паники) ===================== */

/*
 Ошибка ─► errorList (читается снаружи через errors()).
 Вторую ошибку на той же строке и позиции не пишем — это следствие первой.
*/
void Parser::report(const SyntaxError& e) {
	if (!errorList.empty() && errorList.back().line == e.line && errorList.back().col == e.col) return;
	errorList.push_back(e);
}

/*
 * Вызывается из catch в parseBlock. Пропускает остаток оператора с ошибкой.
 * Остановка:
 *   «;»                                   — забираем, оператор кончился;
 *   «}», закрывающая пропущенный блок     — забираем, оператор кончился;
 *   «}» самого блока                      — НЕ забираем, её ждёт parseBlock;
 *   тип, if, while, return                — НЕ забираем, с них начнётся следующий оператор
 *                                           (например, когда забыли «;»).
 * Зацикливания нет: без продвижения выходим только на «}» (цикл parseBlock закончится)
 * или на лексеме, которую следующий parseStatement гарантированно заберёт.
 */
void Parser::syncStatement() {
	int depth = 0;                                         // глубина пропущенных «{ }»
	while (!check(TokenCode::EndOfFile)) {
		if (check(TokenCode::LFigParent)) {
			++depth;
		}
		else if (check(TokenCode::RFigParent)) {
			if (depth == 0) return;
			if (--depth == 0) { 
				advance(); return; 
			}
		}
		else if (depth == 0) {
			if (check(TokenCode::DotComm)) {
				advance();
				return;
			}
			if (checkAny(types) || checkAny({ TokenCode::kwIf, TokenCode::kwWhile, TokenCode::kwReturn }))
				return;
		}
		advance();
	}
}

/*
 * Вызывается из catch в parseProgram. Пропускает лексемы до типа вне фигурных
 * скобок — с него начнётся следующая функция. Хотя бы одна лексема пропускается
 * всегда (do-while), иначе можно зациклиться на той же ошибке.
 */
void Parser::syncFunction() {
	int depth = 0;
	do {
		if (check(TokenCode::LFigParent)) ++depth;
		else if (check(TokenCode::RFigParent) && depth > 0) --depth;
		advance();
	} while (!check(TokenCode::EndOfFile) && !(depth == 0 && checkAny(types)));
}
