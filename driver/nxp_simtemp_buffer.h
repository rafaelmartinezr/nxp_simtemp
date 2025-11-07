#ifndef NXP_SIMTEMP_BUFFER
#define NXP_SIMTEMP_BUFFER

#include "nxp_simtemp.h"

#define BUFFER_CAPACITY (128) // should be a power of 2

int init_ring_buffer(void);
void destroy_ring_buffer(void);
void ring_buffer_push(struct simtemp_sample* entry);
int ring_buffer_peek(size_t index, struct simtemp_sample *out_sample);
int ring_buffer_peek_latest(struct simtemp_sample *out_sample);
void clear_ring_buffer(void);
size_t get_ring_buffer_size(void);

#endif
