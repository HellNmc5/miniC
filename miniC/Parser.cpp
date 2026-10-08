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
}

Parser::Parser(const string& source) : lexer(source) {
	advance();   // сразу читаем первую лексему
}

// Перейти к следующей лексеме. Лексемы-ошибки пропускаются
void Parser::advance() {
	cur = lexer.nextToken();
	while (cur.code == TokenCode::Error) {
		cur = lexer.nextToken();
	}
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
	NodePtr left = parseBinary(level + 1);                 // левый операнд — с этажа сильнее
	while (checkAny(levels[level])) {                      // пока стоит операция этого этажа
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
		advance();
		if (!match(TokenCode::LParent)) {                  // за именем нет «(»
			return makeNode(NodeKind::Var, name);          //   — это переменная
		}
		NodePtr call = makeNode(NodeKind::Call, name);     
		parseArguments(*call);                        
		return call;
	}

	// 3. выражение в скобках: (2 + 3)
	if (match(TokenCode::LParent)) {                       // забрали «(»
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
		advance();
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
	NodePtr n = makeNode(NodeKind::Block,cur);
	advance();
	while(!check(TokenCode::RFigParent) && !check(TokenCode::EndOfFile)){
		n->children.push_back(parseStatement());
	}
	expect(TokenCode::RFigParent, "«}» после блока");
	return n;
}
/*
<объявление> ::= <тип> идентификатор ";"
@return Указатель на узел дерева разбора, представляющий объявление переменной
*/
NodePtr Parser::parseVarDecl() {
	TokenCode type = expectType("типа переменной");
	Token name = expect(TokenCode::Identfier, "имени переменной");
	NodePtr n = makeNode(NodeKind::VarDecl, name);
	n->type = type;
	if(match(TokenCode::OpAssign)) {
		n->children.push_back(parseExpression());
	}
	expect(TokenCode::DotComm, "«;» после объявления переменной");
	return n;
}
/*
<type> ::= "int" | "word" | "bool"
@return Код типа переменной
*/
TokenCode Parser::expectType(const std::string& what) {
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
	TokenCode type = expectType("типа функции");

	if(!checkAny({ TokenCode::Identfier, TokenCode::kwMain })){
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

	n->children.push_back(parseBlock());
	return n;
}
/*
<param> ::= <тип> идентификатор
@return Указатель на узел дерева разбора, представляющий параметр функции
*/
NodePtr Parser::parseParam() {
	TokenCode type = expectType("типа параметра");
	Token name = expect(TokenCode::Identfier, "имени параметра");
	NodePtr n = makeNode(NodeKind::Param, name);
	n->type = type;
	return n;
}
// Разбор всей программы: <программа> ::= <функция> { <функция> }
NodePtr Parser::parseProgram() {
	NodePtr programNode = makeNode(NodeKind::Program, Token{});
	while (!check(TokenCode::EndOfFile)) {
		programNode->children.push_back(parseFunction());
	}
	return programNode;
}