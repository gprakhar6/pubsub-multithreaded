#ifndef __PUBSUB_H__
#define __PUBSUBU_H__

#include <stdint.h>

#define MAX_TOPIC_NAME_LEN (64)
#define MAX_NUM_TOPIC (128)
#define MAX_NUM_SUBSCRIBER (512)
#define MAX_MEM_POOL (4 * 1024 * 1024)
#define MAX_POLL_DATA_RETIRES 100000

typedef struct {
    char name[MAX_TOPIC_NAME_LEN];
    uint8_t *queue;
    uint32_t elem_sz;
    uint32_t num_elem;
    volatile uint32_t head_ptr;
    volatile uint64_t pub_count0;
    volatile uint64_t pub_count1;
    /******************/
    uint32_t next_topic;
    uint32_t next_free_topic;
} topic_t;

typedef enum {
    RESET_TO_LAST_VALUE, // with this sub will get most old data
    RESET_TO_FIRST_VALUE, // with this sub will get most new data
    RESET_TO_NEW_VALUE // with this sub will get next new data
} reset_pos_t;

typedef struct {
    topic_t *topic_ptr;
    uint32_t tail_ptr;
    uint64_t next_rd_count;
    reset_pos_t rst_pos;
    /******************/
    uint32_t next_subscriber;
    uint32_t next_free_subscriber;
} subscriber_t;

void init_pubsub();
topic_t* allocate_topic(char *name, uint32_t elem_sz,
			uint32_t num_elem);
subscriber_t* allocate_subscriber(char *name, reset_pos_t rst_pos);
void list_all_topics();
void pub_data(void *d, topic_t *t);
int poll_data(void *d, subscriber_t *s);
int peek_data(void *d, subscriber_t *s);
#endif
