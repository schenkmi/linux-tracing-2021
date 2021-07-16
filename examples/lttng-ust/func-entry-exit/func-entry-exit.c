/**
 * Example for doing call trace with liblttng-ust-cyg-profile.so
 * build with:
 * gcc -g -O2 -finstrument-functions func-entry-exit.c -o func-entry-exit
 */
#include <stdio.h>

int fibonacci(int i)
{
    if (i == 0) {
        return 0;
    }

    if (i == 1) {
        return 1;
    }

    return fibonacci(i-1) + fibonacci(i-2);
}

int main(int argc, char *argv[])
{
    int i;

    for (i = 0; i < 10; i++) {
        printf("%d\t\n", fibonacci(i));
    }

    return 0;
}
