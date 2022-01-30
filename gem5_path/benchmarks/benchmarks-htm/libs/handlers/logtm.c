#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "logtm.h"
#include "thread_context.h"

#define _unused(x) ((void)(x))

/*********************http://www.ciphersbyritter.com***************************/

struct
{
    char pad[64];
    unsigned long z, w, jsr, jcong;
    char pad2[64];
}g_rand;


#define znew  ((g_rand.z=36969*(g_rand.z&65535)+(g_rand.z>>16))<<16)
#define wnew  ((g_rand.w=18000*(g_rand.w&65535)+(g_rand.w>>16))&65535)
#define MWC   (znew+wnew)
#define SHR3  (g_rand.jsr=(g_rand.jsr=(g_rand.jsr=g_rand.jsr^(g_rand.jsr<<17))^(g_rand.jsr>>13))^(g_rand.jsr<<5))
#define CONG  (g_rand.jcong=69069*g_rand.jcong+1234567)
#define KISS  ((MWC^CONG)+SHR3)

/**********************/

unsigned long power_of_2(unsigned long a){
    unsigned long val;
    val = 1;
    val <<= a;
    return val;
}

unsigned long compute_backoff(unsigned long num_retries){
    unsigned long backoff = 0;
    unsigned long max_backoff;

    if (num_retries > 16)
        max_backoff = 64 * 1024 + (num_retries - 16);
    else
        max_backoff = power_of_2(num_retries);

    backoff = max_backoff;

    return backoff;
}

long randomized_backoff(unsigned long num_retries){
    volatile long a[32];
    volatile long b;
    long j;
    long backoff = (unsigned long) (CONG) % compute_backoff(num_retries);
    for (j = 0; j < backoff; j++){
        b += a[j % 32];
    }
    return b;
}

void init_g_rand(){g_rand.z=362436069; g_rand.w=521288629; g_rand.jsr=123456789; g_rand.jcong=380116160;}


void walk_log(unsigned long *log){
    int i;
    char *ptr = (char *)log;
    for (i = 0; i < MAX_LOG_SIZE_BYTES; i+= LOG_PAGE_SIZE_BYTES)
        ptr[i] = '\0';
}


void init_log(_tm_thread_context_t *thread_contexts){
    long i,j;
    _unused(j);

    for (i = 0; i < thread_contexts->info.numThreads; i++) {
        // Point transaction log in thread context used by "standard" abort handler
        _tm_thread_context_t *ctx = &thread_contexts[i];
        int ret = posix_memalign (&(ctx->info.logtm_transactionLog), LOG_PAGE_SIZE_BYTES,
                                  MAX_LOG_SIZE_BYTES);
        if (ret != 0) {
            perror("Cannot allocate aligned memory for transaction log");
        }
        walk_log(ctx->info.logtm_transactionLog);
    }
}

void logtm_init_transaction_state(void *thread_contexts){
    init_g_rand();
    init_log((_tm_thread_context_t *)thread_contexts);
}
