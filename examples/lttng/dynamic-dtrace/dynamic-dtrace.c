/**
 * LTTng dynamic user space tracing example
 * build with gcc -g -O2 dynamic-dtrace.c -o dynamic-dtrace
 * 
 * Precondition: sudo apt-get install systemtap-sdt-dev
 */
 
#include <sys/sdt.h>

void func(void)
{
    DTRACE_PROBE(my_provider, my_probe);
}

int main(int argc, char **argv)
{
    func();
    return 0;
}

