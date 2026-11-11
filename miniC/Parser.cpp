#include "Parser.h"
#include <vector>

using namespace std;

namespace {
	const vector<vector<TokenCode>> levels = {
		{ TokenCode::OpLogOr },                                   // ||
		{ TokenCode::OpLogAnd },                                  // &&
		{ TokenCode::OpEq, TokenCode::OpNotAssign },              // == !=
		{ TokenCode::OpMenee, TokenCode::OpBolee,
		  TokenCode::OpMenAssi, TokenCode::OpBolAssi },           // < > <= >=
		{ TokenCode::OpMoveLeft, TokenCode::OpMoveRight },        // << >>
		{ TokenCode::OpPlus, TokenCode::OpMinus },                // + -
		{ TokenCode::OpMult, TokenCode::OpDiv, TokenCode::OpDivPerc },  // * / %
	};

	const vector<TokenCode> types = { TokenCode::kwInt, TokenCode::kwWord, TokenCode::kwBool };

	// Имена нетерминалов для строк таблицы levels — узлы дерева разбора
	const char* const levelNames[] = { "Или", "И", "Равенство", "Сравнение", "Сдвиг", "Сумма", "Произведение" };
}

Parser::Parser(const string& source) : lexer(source) {
	advance();   // сразу читаем первую лексему
}

// Перейти к следующей лексеме. Лексемы-ошибки пропускаются
void Parser::advance() {
	cstLeaf(cur);   // забираемая лексема уходит листом в дерево разбора
	cur = lexer.nextToken();
	while (cur.code == TokenCode::Error) {
		cur = lexer.nextToken();
	}
}

/*
 Лексема — листом в текущий узел дерева разбора.
 Идентификатор и константа в грамматике — нетерминалы <идентификатор> и <константа>
 (лексический уровень), поэтому для них сначала узел, а лексема — под ним.
*/
void Parser::cstLeaf(const Token& t) {
	if (cstStack.empty() || recovering || t.code == TokenCode::EndOfFile) return;
	CstNode* parent = cstStack.back();
	const bool ownNode = parent->label == "Имя функции";   // имя функции уже в своём узле — второй не нужен
	if (!ownNode && (t.code == TokenCode::Identfier || t.code == TokenCode::IntConst)) {
		CstPtr wrap(new CstNode());
		wrap->label = t.code == TokenCode::Identfier ? "Идентификатор" : "Константа";
		parent->children.push_back(move(wrap));
		parent = parent->children.back().get();
	}
	CstPtr leaf(new CstNode());
	leaf->label = t.text;
	leaf->terminal = true;
	leaf->token = t;
	parent->children.push_back(move(leaf));
}

// Открыть узел нетерминала: первый становится корнем, остальные — потомками текущего
Parser::CstScope::CstScope(Parser& parser, const char* name) : p(parser) {
	CstPtr node(new CstNode());
	node->label = name;
	CstNode* raw = node.get();
	if (p.cstStack.empty()) p.cstRoot = move(node);
	else p.cstStack.back()->children.push_back(move(node));
	p.cstStack.push_back(raw);
}

// Закрыть узел: текущим снова становится родитель
Parser::CstScope::~CstScope() {
	p.cstStack.pop_back();
}

size_t Parser::cstMark() const {
	return cstStack.empty() ? 0 : cstStack.back()->children.size();
}

// Узлы текущего узла начиная с mark — в новый узел name, он становится текущим
void Parser::CstLift::wrap(const char* name, size_t mark) {
	if (active || p.cstStack.empty() || p.recovering) return;
	CstNode* parent = p.cstStack.back();
	CstPtr node(new CstNode());
	node->label = name;
	for (size_t i = mark; i < parent->children.size(); ++i)
		node->children.push_back(move(parent->children[i]));
	parent->children.resize(mark);
	CstNode* raw = node.get();
	parent->children.push_back(move(node));
	p.cstStack.push_back(raw);
	active = true;
}

Parser::CstLift::~CstLift() {
	if (active) p.cstStack.pop_back();
}

// Текущая лексема такого вида?
//check(TokenCode::DotComm) — стоит ли сейчас «;»
bool Parser::check(TokenCode code) const {
	return cur.code == code;
}

bool Parser::match(TokenCode code) {
	if (check(code)) {
		advance();
		return true;
	}
	return false;
}

Token Parser::expect(TokenCode code, const string& what) {
	if (check(code)) {
		Token t = cur;
		advance();
		return t;
	}
	error("Ожидалось " + what);
}

void Parser::error(const string& message) {
	string got = cur.code == TokenCode::EndOfFile ? "конец файла" : "«" + cur.text + "»";
	throw SyntaxError{ message + ", а встретилось " + got, cur.line, cur.colStart };
}

// Текущая лексема — одна из перечисленных?
bool Parser::checkAny(const vector<TokenCode>& codes) const {
	for (TokenCode c : codes)
		if (check(c)) return true;
	return false;
}

NodePtr Parser::makeBinary(const Token& op, NodePtr left, NodePtr right) {
	NodePtr n = makeNode(NodeKind::Binary, op);
	// Собрать узел двухместной операции: op с детьми left и right.
	// Для 2 + 3: узел «+», children[0] = 2, children[1] = 3. Порядок важен — для «-» это a - b, а не b - a.
	n->children.push_back(move(left));
	n->children.push_back(move(right));
	return n;
}
//Парсер выражения
NodePtr Parser::parseExpression() {
	return parseBinary(0);
}

// Один этаж двухместных операций из таблицы levels.
// <этаж> ::= <этаж ниже> { операция_этажа <этаж ниже> }
NodePtr Parser::parseBinary(size_t level) {
	if (level == levels.size()) {                          // этажи кончились —
		return parseUnary();                               //   дальше только операнд
	}
	size_t mark = cstMark();                               // отсюда в дереве разбора начнётся левый операнд
	CstLift lift(*this);
	NodePtr left = parseBinary(level + 1);                 // левый операнд — с этажа сильнее
	while (checkAny(levels[level])) {                      // пока стоит операция этого этажа
		lift.wrap(levelNames[level], mark);                //   есть знак — появляется узел этажа («Сумма» и т.п.)
		Token op = cur;                                    //   запомнили знак
		advance();                                         //   забрали его
		NodePtr right = parseBinary(level + 1);            //   правый операнд — тоже с этажа сильнее
		left = makeBinary(op, move(left), move(right));    //   всё собранное — левая часть следующей операции
	}
	return left;
}

// <унарное> ::= ( "-" | "!" ) <унарное> | <первичное>
NodePtr Parser::parseUnary() {
	if (checkAny({ TokenCode::OpMinus, TokenCode::OpLogNot })) {   // перед операндом стоит - или !
		CstScope s(*this, "Унарное");
		Token op = cur;                                    // запомнили знак
		advance();                                         // забрали его
		NodePtr n = makeNode(NodeKind::Unary, op);         // узел операции
		n->children.push_back(parseUnary());               // после знака снова операнд (так работают !!a, --x)
		return n;
	}
	return parsePrimary();                                 // знака нет — сразу операнд
}

// <первичное> ::= константа | true | false
//               | идентификатор [ "(" [ <аргументы> ] ")" ]
//               | "(" <выражение> ")"
NodePtr Parser::parsePrimary() {
	// 1. константа: 5, true, false
	if (checkAny({ TokenCode::IntConst, TokenCode::BoolTrue, TokenCode::BoolFalse })) {
		Token t = cur;
		advance();
		return makeNode(NodeKind::Const, t);
	}

	// 2. имя: переменная или вызов функции
	if (check(TokenCode::Identfier)) {
		Token name = cur;                                  // запомнили имя
		size_t mark = cstMark();
		advance();
		if (!check(TokenCode::LParent)) {                  // за именем нет «(»
			return makeNode(NodeKind::Var, name);          //   — это переменная
		}
		CstLift call(*this);
		call.wrap("Вызов", mark);                          // имя и скобки с аргументами — в узел «Вызов»
		advance();                                         // забрали «(»
		NodePtr callNode = makeNode(NodeKind::Call, name);
		parseArguments(*callNode);
		return callNode;
	}

	// 3. выражение в скобках: (2 + 3)
	if (check(TokenCode::LParent)) {
		CstScope s(*this, "Скобки");                       // «(», выражение, «)» — в один узел
		advance();                                         // забрали «(»
		NodePtr inner = parseExpression();                 // разобрали содержимое с самого верхнего этажа
		expect(TokenCode::RParent, "«)»");                 // скобка обязана закрыться
		return inner;                                      // узла для скобок нет — только содержимое
	}

	// 4. ничего не подошло
	error("Ожидалось выражение");
}

// <оператор> ::= <объявление> | <if> | <while> | <return> | <блок>
//              | идентификатор <продолжение>
/*
Смотрим на первую лексему и решаем, какой оператор начинается.
Если это имя — забираем его и смотрим на следующую лексему:
«=» — присваивание, «(» — вызов функции, иначе ошибка.
 */
NodePtr Parser::parseStatement() {
	if (checkAny({ TokenCode::kwInt, TokenCode::kwWord, TokenCode::kwBool })) return parseVarDecl();
	if (check(TokenCode::kwIf))        return parseIf();
	if (check(TokenCode::kwWhile))     return parseWhile();
	if (check(TokenCode::kwReturn))    return parseReturn();
	if (check(TokenCode::LFigParent))  return parseBlock();

	if (check(TokenCode::Identfier)) {
		Token name = cur;
		size_t mark = cstMark();
		advance();
		CstLift c(*this);                                  // имя и всё после него — в узел оператора
		if (check(TokenCode::OpAssign))     c.wrap("Присваивание", mark);
		else if (check(TokenCode::LParent)) c.wrap("Вызов", mark);
		// <продолжение> ::= "=" <выражение> ";"
		if (match(TokenCode::OpAssign)) {
			NodePtr n = makeNode(NodeKind::Assign, name);
			n->children.push_back(parseExpression());
			expect(TokenCode::DotComm, "«;»");
			return n;
		}
		//                 | "(" [ <аргументы> ] ")" ";"
		if (match(TokenCode::LParent)) {
			NodePtr n = makeNode(NodeKind::Call, name);
			parseArguments(*n);
			expect(TokenCode::DotComm, "«;»");
			return n;
		}
		error("Ожидалось «=» или «(»");
	}

	error("Ожидался оператор");
}

void Parser::parseArguments(Node& call){
	CstLift args(*this);
	args.wrap("Аргументы", cstMark() - 1);             // «(» уже забрана вызывающим — это последний лист; узел — от неё до «)»
	if(!check(TokenCode::RParent)){// есть «(» — это вызов, сразу «)» — аргументов нет; иначе разбираем их
		call.children.push_back(parseExpression());//   первый аргумент
		while(match(TokenCode::Comm)){//   пока есть запятая —
			call.children.push_back(parseExpression());//   след аргумент
		}
	}
	expect(TokenCode::RParent, "«)»");
}
/* 
<if> ::= "if" "(" <выражение> ")" <оператор> ["else" <оператор> ]
@return Указатель на узел дерева разбора, представляющий if-оператор
*/
NodePtr Parser::parseIf(){
	CstScope s(*this, "Условие");
	NodePtr n = makeNode(NodeKind::If,cur);
	advance();
	expect(TokenCode::LParent, "«(» после if");
	n->children.push_back(parseExpression());
	expect(TokenCode::RParent, "«)» после условия if");
	n->children.push_back(parseStatement());
	if(match(TokenCode::kwElse)){
		n->children.push_back(parseStatement());
	}
	return n;
}
/*
<while> ::= "while" "(" <выражение> ")" <оператор>
@return Указатель на узел дерева разбора, представляющий while-оператор
*/
NodePtr Parser::parseWhile(){
	CstScope s(*this, "Цикл");
	NodePtr n = makeNode(NodeKind::While,cur);
	advance();
	expect(TokenCode::LParent, "«(» после while");
	n->children.push_back(parseExpression());
	expect(TokenCode::RParent, "«)» после условия while");
	n->children.push_back(parseStatement());
	return n;
}
/*
<return> ::= "return" <выражение> ";"
@return Указатель на узел дерева разбора, представляющий return-оператор
*/
NodePtr Parser::parseReturn(){
	CstScope s(*this, "Возврат");
	NodePtr n = makeNode(NodeKind::Return,cur);
	advance();
	n->children.push_back(parseExpression());
	expect(TokenCode::DotComm, "«;» после return");
	return n;
}
/*
<block> ::= "{" <оператор> { <оператор> } "}"
@return Указатель на узел дерева разбора, представляющий блок
*/
NodePtr Parser::parseBlock(){
	CstScope s(*this, "Блок");
	NodePtr n = makeNode(NodeKind::Block,cur);
	advance();
	while(!check(TokenCode::RFigParent) && !check(TokenCode::EndOfFile)){
		try {
			n->children.push_back(parseStatement());
		}
		catch (const SyntaxError& e) {      // оператор с ошибкой: запомнили её,
			report(e);                      //   пропустили остаток оператора
			recovering = true;              //   (пропущенное в дерево разбора не пишем)
			syncStatement();                //   и разбираем следующий
			recovering = false;
		}
	}
	expect(TokenCode::RFigParent, "«}» после блока");
	return n;
}
/*
<объявление> ::= <тип> идентификатор ";"
@return Указатель на узел дерева разбора, представляющий объявление переменной
*/
NodePtr Parser::parseVarDecl() {
	CstScope s(*this, "Объявление");
	TokenCode type = expectType("переменной");
	Token name = expect(TokenCode::Identfier, "имя переменной");
	NodePtr n = makeNode(NodeKind::VarDecl, name);
	n->type = type;
	if (match(TokenCode::OpAssign)) n->children.push_back(parseExpression());
	expect(TokenCode::DotComm, "«;»");
	return n;
}
/*
<type> ::= "int" | "word" | "bool"
@return Код типа переменной
*/
TokenCode Parser::expectType(const std::string& what) {
	CstScope s(*this, "Тип");
	if (checkAny({ TokenCode::kwInt, TokenCode::kwWord, TokenCode::kwBool })) {
		TokenCode type = cur.code;
		advance();
		return type;
	}
	error("Ожидался тип " + what);
}
/*
<function> ::= <тип> идентификатор "(" { <тип> идентификатор { "," <тип> идентификатор } } ")" <блок>
@return Указатель на узел дерева разбора, представляющий функцию
*/
NodePtr Parser::parseFunction() {
	CstScope s(*this, "Функция");
	TokenCode type = expectType("функции");

	if(!checkAny({ TokenCode::Identfier, TokenCode::kwMain })){
		error("Ожидалось имя функции");
	}
	NodePtr n = makeNode(NodeKind::Function, cur);
	n->type = type;
	{
		CstScope name(*this, "Имя функции");             // имя — в свой узел
		advance();
	}

	expect(TokenCode::LParent, "«(» после имени функции");
	{
		CstLift params(*this);
		params.wrap("Параметры", cstMark() - 1);           // узел от «(» до «)», как на доске
		if (!check(TokenCode::RParent)) {
			n->children.push_back(parseParam());
			while (match(TokenCode::Comm)) {
				n->children.push_back(parseParam());
			}
		}
		expect(TokenCode::RParent, "«)» после списка параметров");
	}

	n->children.push_back(parseBlock());
	return n;
}
/*
<param> ::= <тип> идентификатор
@return Указатель на узел дерева разбора, представляющий параметр функции
*/
NodePtr Parser::parseParam() {
	TokenCode type = expectType("параметра");
	Token name = expect(TokenCode::Identfier, "имени параметра");
	NodePtr n = makeNode(NodeKind::Param, name);
	n->type = type;
	return n;
}
// Разбор всей программы: <программа> ::= { <функция> }
NodePtr Parser::parseProgram() {
	CstScope s(*this, "Программа");                      // корень дерева разбора
	NodePtr programNode = makeNode(NodeKind::Program, Token{});
	while (!check(TokenCode::EndOfFile)) {
		try {
			if (!checkAny(types)) {        // функция может начинаться только с типа
				error("Лишняя лексема вне функций: ожидалось описание функции или конец файла");
			}
			programNode->children.push_back(parseFunction());
		}
		catch (const SyntaxError& e) {      // ошибка, которую не поймал блок (заголовок функции,
			report(e);                      //   незакрытый блок, лишняя лексема) —
			recovering = true;
			syncFunction();                 //   переходим к следующей функции
			recovering = false;
		}
	}
	return programNode;
}

// Записать ошибку. Вторую ошибку на том же месте не пишем — это следствие первой
void Parser::report(const SyntaxError& e) {
	if (!errorList.empty() && errorList.back().line == e.line && errorList.back().col == e.col) return;
	errorList.push_back(e);
}

/*
Восстановление внутри блока (режим паники): пропускаем лексемы до конца оператора.
Остановка: «;» (забираем), «}», закрывающая пропущенный вложенный блок (забираем),
или лексема, с которой начинается следующий оператор: тип, if, while, return (не забираем).
«}» самого блока тоже не забираем: его ждёт parseBlock.
*/
void Parser::syncStatement() {
	int depth = 0;                                         // глубина пропущенных «{ }»
	while (!check(TokenCode::EndOfFile)) {
		if (check(TokenCode::LFigParent)) {
			++depth;
		}
		else if (check(TokenCode::RFigParent)) {
			if (depth == 0) return;                        // «}» текущего блока — остановились перед ней
			if (--depth == 0) { advance(); return; }       // закрыли пропущенный блок — оператор кончился
		}
		else if (depth == 0) {
			if (check(TokenCode::DotComm)) {               // «;» — конец оператора, забрали её
				advance();
				return;
			}
			if (checkAny(types) || checkAny({ TokenCode::kwIf, TokenCode::kwWhile, TokenCode::kwReturn }))
				return;                                    // начало следующего оператора (например, забыли «;»)
		}
		advance();
	}
}

/*
Восстановление на верхнем уровне: пропускаем лексемы до типа, с которого начнётся
следующая функция (вне фигурных скобок). Хотя бы одну лексему пропускаем всегда,
иначе можно зациклиться на той же ошибке.
*/
void Parser::syncFunction() {
	int depth = 0;
	do {
		if (check(TokenCode::LFigParent)) ++depth;
		else if (check(TokenCode::RFigParent) && depth > 0) --depth;
		advance();
	} while (!check(TokenCode::EndOfFile) && !(depth == 0 && checkAny(types)));
}
