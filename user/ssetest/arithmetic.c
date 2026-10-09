// Volatile operands prevent folding: user flags must generate scalar SSE/SSE2.
int check_arithmetic(void) {
    volatile float a = 1.5f;
    volatile float b = 2.0f;
    volatile double c = 2.25;
    volatile double d = 4.0;
    return a * b + a == 4.5f && c * d + c == 11.25;
}
