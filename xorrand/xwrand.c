#include <stdint.h>

struct xwrand_state {
    uint32_t a, b, c, d, e;
    uint32_t counter;
};

typedef struct xwrand_state xwrand_t;

/* The state array must be initialized to not be all zero in the first four words */
uint32_t xwrand(xwrand_t *state) {
    /* Algorithm "xorwow" from p. 5 of Marsaglia, "Xorshift RNGs" */
    uint32_t t  = state->e;
 
    uint32_t s  = state->a;  /* Perform a contrived 32-bit shift. */
    state->e = state->d;
    state->d = state->c;
    state->c = state->b;
    state->b = s;
 
    t ^= t >> 2;
    t ^= t << 1;
    t ^= s ^ (s << 4);
    state->a = t;
    state->counter += 362437;
    return t + state->counter;
}
