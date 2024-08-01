#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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

void xwrand_seed(xwrand_t *state, uint32_t seedval) {
        state->a ^= seedval;
        xwrand(state);
}

xwrand_t xwrand_init(uint32_t initval) {
        xwrand_t ret = {452764364, 706985783, 2521395330, 1263432680, 2960490940, 2680793543};
	/* This algo cant work with 0 initialized values. If we generate some sequence we will see that they arent quite random, so authors advise to use some numbers from other random gen's in init. Tests and plots are provided in this folder. */
        xwrand_seed(&ret, initval);
        return ret;
}

int main(int argc, char *argv[]) {
	long num = 0;
	char _dummy;

	if (argc != 2 || 1 != sscanf(argv[1], "%ld%c", &num, &_dummy) || num <= 0) {
		fprintf(stderr, "Exactly one argument is expected\n");
		exit(EXIT_FAILURE);
	}

	num--;
	xwrand_t state = xwrand_init(0);
	printf("%lu, ", (unsigned long)xwrand(&state));

	for (int i = 0; i < num; i ++) {
		printf("%lu, ", (unsigned long)xwrand(&state));
	}
	printf("\n");
	return 0;
}
