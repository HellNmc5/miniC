#pragma once
#include <string>
#include <vector>
#include "Ast.h"

/**
 * @brief 
 * 
 * TriadOp - Множество содержащее операции триад
 */
enum class TriadOp{
    Proc, Ret,// Процедура, возврат
    Assign,// Присваивание
    Add, Sub, Mul, Div, Mod, Shl, Shr,// Арифметические операции: сложение, вычитание, умножение, деление, остаток от деления, сдвиг влево, сдвиг вправо
    Lt, Gt, Le, Ge, Eq, Ne,//Сравнения
    And, Or, Not, Neg,//Логические операции, Neg - унарный минус
    Param, Call,//Аргумент вызова, вызов функции
    Jmp, Jf,//Переход всегда, переход если условие ложно
    C//Константа
};
/**
 * @brief 
 * 
 * Множество видов операндов
 */
enum class OperandKind{
    None,//Операнда нет
    Const,//Число: value
    Var,//Переменная: decl - Объявление её
    Ref,//Результат другой триады: value - её номер
    Target,//Куда переходить: value - номер триады
    Func//функция: decl - объявление
};

struct Operand
{
    OperandKind kind = OperandKind::None;//Тип операнда
    int value = 0;//const - число; Ref, Target - номер триады
    const Node* decl = nullptr;//Var, Func - объявления
};
/**
 * @brief 
 * 
 * Структура триады
 */
struct Triad
{
    TriadOp op;//Операция
    Operand a,b;//Операнды
    TokenCode type{};//Тип результата: int, word, bool - генератору нужен для знаковых команд
};

std::string triadText(const Triad& t, int number); //Строка вида: 8: * n, ^7
void printTriads(const std::vector<Triad>& code); //Вывести список

class TriadBuilder{
public:
    std::vector<Triad> build(const Node& program);//Все функции программы в 1 список
private:
    std::vector<Triad> code;
    int emit(TriadOp op, Operand a = {}, Operand b = {}, TokenCode type = {});//Дописать триаду и вернуть её номер
    int next() const {return (int) code.size()+1;}//Номер который получит следующая триада
    void patch(int number, int to);//Вписать переход number цель to

    Operand getExpr(const Node& n); // Выражение -> операнд с его результатом
    void getStmt(const Node& n);//Оператор -> триады
    void getFunction(const Node& n);//Функция -> триады
};