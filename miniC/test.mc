int fact(int n) {
    if (n <= 1) { return 1; }
    return n * fact(n - 1);
}

int main() {
    int r = fact(5);
    word mask = 1 << 3;
    bool ok = r > 100 && mask != 0;
    if (ok) { return 0; } else { return 1; }
}