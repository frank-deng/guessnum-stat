#ifndef __guessnum_h__
#define __guessnum_h__

#include <stdint.h>
#include <stdbool.h>
#include "random.h"

#define GUESS_CHANCES (16)

typedef struct{
    unsigned long long report_s[GUESS_CHANCES];
    unsigned long long report_m[GUESS_CHANCES];
}game_data_t;

#ifdef __cplusplus
extern "C" {
#endif

void init();
uint8_t guess(rand_t *rand,bool mastermind);

#ifdef __cplusplus
}
#endif

#endif
