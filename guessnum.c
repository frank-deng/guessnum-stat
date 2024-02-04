#include "guessnum.h"

#if !defined(min)
#define min(x, y) ((x) < (y) ? (x) : (y))
#endif

#define NUM_UNIQUE_COUNT 5040
#define NUM_TOTAL_COUNT 10000

static uint16_t nums[NUM_TOTAL_COUNT];
static uint8_t table_data[(NUM_TOTAL_COUNT)*(NUM_TOTAL_COUNT-1)/2+1];
static uint8_t *table[NUM_TOTAL_COUNT-1];

static inline uint8_t check(uint16_t ans, uint16_t guess)
{
    uint8_t result = 0, i, valAns, valGuess;
    uint8_t existAns[10] = {0,0,0,0,0,0,0,0,0,0};
    uint8_t existGuess[10] = {0,0,0,0,0,0,0,0,0,0};
    for (i = 0; i < 4; i++) {
        valAns = ans & 0xf;
        valGuess = guess & 0xf;
        ans >>= 4;
        guess >>= 4;
        if (valAns == valGuess) {
            result += 0x10;
            continue;
        }
        existAns[valAns]++;
        existGuess[valGuess]++;
    }
    for (i = 0; i < 10; i++) {
        result += min(existAns[i], existGuess[i]);
    }
    return result;
}
static inline uint16_t uint2bcd(uint16_t n, bool *digit_unique_out)
{
    uint16_t digit=(n%10),result=digit,map=(1<<digit);
    bool digit_unique=true;

    n/=10;
    digit=n%10;
    if ((map & (1<<digit)) != 0) {
        digit_unique=false;
    }
    map |= (1<<digit);
    result |= (digit<<4);

    n/=10;
    digit=n%10;
    if ((map & (1<<digit)) != 0) {
        digit_unique=false;
    }
    map |= (1<<digit);
    result |= (digit<<8);

    n/=10;
    digit=n%10;
    if ((map & (1<<digit)) != 0) {
        digit_unique=false;
    }
    result |= (digit<<12);

    if(NULL!=digit_unique_out) {
        *digit_unique_out=digit_unique;
    }

    return result;
}
void init()
{
    uint16_t n, nbcd, *n_unique=nums, *n_dup=nums+NUM_UNIQUE_COUNT;
    uint16_t i,j;
    bool digit_unique=true;
    for(n=0; n<NUM_TOTAL_COUNT; n++) {
        nbcd=uint2bcd(n,&digit_unique);
        if(digit_unique) {
            *n_unique=nbcd;
            n_unique++;
        } else {
            *n_dup=nbcd;
            n_dup++;
        }
    }
    
    uint8_t *p_table=table_data;
    for(i=0; i<NUM_TOTAL_COUNT-1; i++){
        table[i]=p_table;
        for(j=i+1; j<NUM_TOTAL_COUNT; j++){
            *p_table=check(nums[i],nums[j]);
            p_table++;
        }
    }
}
static inline uint8_t tcheck(uint16_t a,uint16_t b)
{
    if(a==b){
        return 0x40;
    }else if(a>b){
        return table[b][a-b-1];
    }else{
        return table[a][b-a-1];
    }
}
uint8_t guess(rand_t *rand,bool mastermind)
{
    uint16_t count=(mastermind ? NUM_TOTAL_COUNT : NUM_UNIQUE_COUNT);
    uint16_t ans=(getRandom(rand)%count), inums[NUM_TOTAL_COUNT];
    uint16_t n,n2,*np,*np2,*np_end;
    uint8_t times=0,cmp;
    while (times < GUESS_CHANCES && count>0) {
        times++;
        if(1==times){
            n=getRandom(rand)%count;
        }else{
            n=inums[getRandom(rand)%count];
        }
        if(n==ans) {
            break;
        }
        cmp=tcheck(ans,n);

        if(1==times) {
            // First guess
            np=inums;
            for(n2=0; n2<count; n2++){
                if(tcheck(n2,n)==cmp){
                    *np=n2;
                    np++;
                }
            }
            count=np-inums;
        } else {
            np=np2=inums;
            np_end=np+count;
            while(np<np_end) {
                n2=*np;
                if(tcheck(n2,n)==cmp) {
                    *np2=n2;
                    np2++;
                }
                np++;
            }
            count=np2-inums;
        }
    }
    if(times>=GUESS_CHANCES || 0==count) {
        return GUESS_CHANCES;
    }
    return times;
}
