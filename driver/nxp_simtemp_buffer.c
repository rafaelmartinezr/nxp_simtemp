#include "nxp_simtemp_buffer.h"
#include <linux/slab.h>
#include <linux/spinlock.h>

#define INDEX_MASK  (BUFFER_CAPACITY - 1)

#define PEEK_ADVANCE_PTR(ptr)  ((ptr + 1) & INDEX_MASK)
#define ADVANCE_PTR(ptr)       ptr = ((ptr + 1) & INDEX_MASK)

struct lifo_ring_buffer {
    size_t head, tail;
    size_t len;
    rwlock_t lock;
    void* buffer;
};

static struct lifo_ring_buffer nxp_simtemp_buffer;

static inline int ring_buffer_is_full(void)
{
    /* Ring buffer is full if head is behind tail */
    return (PEEK_ADVANCE_PTR(nxp_simtemp_buffer.head) == nxp_simtemp_buffer.tail);
}

int init_ring_buffer(void)
{
    nxp_simtemp_buffer.head = 0;
    nxp_simtemp_buffer.tail = 0;
    nxp_simtemp_buffer.len = 0;
    rwlock_init(&nxp_simtemp_buffer.lock);

    nxp_simtemp_buffer.buffer = kzalloc(BUFFER_CAPACITY * sizeof(struct simtemp_sample), GFP_KERNEL);

    if (!nxp_simtemp_buffer.buffer)
        return -ENOMEM;

    return 0;
}

void destroy_ring_buffer(void)
{
    kfree(nxp_simtemp_buffer.buffer);
}

void ring_buffer_push(struct simtemp_sample* entry)
{
    /* Acquire write lock, no bh because the only caller should be the timer callback */
    write_lock(&nxp_simtemp_buffer.lock);

    /* If buffer is full, tail moves one over and entry overwrite the freed space */
    if (ring_buffer_is_full()) {
        ADVANCE_PTR(nxp_simtemp_buffer.tail);
    } else {
        /* Non-full buffer means we can increase the len further */
        nxp_simtemp_buffer.len++;
    }

    (void)memcpy(&((struct simtemp_sample *)nxp_simtemp_buffer.buffer)[nxp_simtemp_buffer.head], 
                entry,
                sizeof(struct simtemp_sample));

    ADVANCE_PTR(nxp_simtemp_buffer.head);
    write_unlock(&nxp_simtemp_buffer.lock);
}

int ring_buffer_peek(size_t index, struct simtemp_sample *out_sample)
{
    /* Acquire read lock */
    read_lock_bh(&nxp_simtemp_buffer.lock);

    if (index >= nxp_simtemp_buffer.len) {
        read_unlock_bh(&nxp_simtemp_buffer.lock);
        return -1;
    }

    size_t offset = (nxp_simtemp_buffer.tail + index) & INDEX_MASK;
    memcpy( out_sample, 
            &((struct simtemp_sample *)nxp_simtemp_buffer.buffer)[offset],
            sizeof(struct simtemp_sample));
    
    read_unlock_bh(&nxp_simtemp_buffer.lock);

    return 0;
}

int ring_buffer_peek_latest(struct simtemp_sample *out_sample)
{
    return ring_buffer_peek(nxp_simtemp_buffer.len-1, out_sample);
}

void clear_ring_buffer(void)
{
    /* Acquire write lock, with bh since could be called outside of softIRQ context */
    write_lock_bh(&nxp_simtemp_buffer.lock);
    nxp_simtemp_buffer.head = 0;
    nxp_simtemp_buffer.tail = 0;
    nxp_simtemp_buffer.len = 0;
    write_unlock_bh(&nxp_simtemp_buffer.lock);
}
