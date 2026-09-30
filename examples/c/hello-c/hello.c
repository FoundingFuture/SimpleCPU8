#include <gpu.h>

/* A diagonal cross and a greeting, the first C program in the IDE. The
 * wrappers take one argument per port byte, so a coordinate is its high
 * byte then its low byte, the way the GPU reads it.
 */
int main(void)
{
    int i;
    gpu_clear(0);
    for (i = 0; i < 200; i = i + 1) {
        gpu_plot(0, i, 0, i, 255);
        gpu_plot(0, 200 - i, 0, i, 28);
    }
    gpu_printf("HELLO FROM C %d", 42);
    return 0;
}
