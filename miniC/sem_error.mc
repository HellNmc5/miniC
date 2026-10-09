int fact(int n) {
    if (n) { return 1; }               // условие не bool
    return n * fact(n - 1, 2);         // лишний аргумент
}

int main() {
    x = 5;                             // x не объявлена
    bool b = 3 + true;                 // число плюс bool
    int y = y + 1;                     // y ещё не объявлена
    int y = 2;                         // повторное объявление
    { int z = 1; }
    z = 2;                             // z не видна вне блока
    fact = 3;                          // присваивание функции
    return b;                          // main возвращает int, а тут bool
}