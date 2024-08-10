#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "pubsub.h"

#define L(x,n) for(x=0;x<n;x++)
#define LEN(x) sizeof(x)/sizeof(x[0])

topic_t topics[MAX_NUM_TOPIC];
uint32_t head_free_topic;
uint32_t head_topic;

subscriber_t subscribers[MAX_NUM_SUBSCRIBER];
uint32_t head_free_subscriber;

uint8_t mem_pool[MAX_MEM_POOL];
uint32_t pool_idx;

void init_pubsub()
{
    int i;
    topics[0].next_free_topic = -1;
    for(i = 1; i < LEN(topics); i++) {
	topics[i].next_free_topic = i - 1;
    }
    head_free_topic = LEN(topics) - 1;
    head_topic = -1;
    
    subscribers[0].next_free_subscriber = -1;
    for(i = 1; i < LEN(subscribers); i++) {
	subscribers[i].next_free_subscriber = i - 1;
    }
    head_free_subscriber = LEN(subscribers) - 1;

    pool_idx = 0;
}

static int new_topic(topic_t **topic)
{
    int fail = 1;
    if(head_free_topic != -1) {
	*topic = &topics[head_free_topic];
	(*topic)->next_topic = head_topic;
	head_topic = head_free_topic;
	head_free_topic
	    = topics[head_free_topic].next_free_topic;
	fail = 0;
    }
    return fail;
}

static int free_topic(topic_t *topic)
{
    int fail = 1;
    uint32_t n, off, idx;
    off = (topic - &topic[0]);
    n = sizeof(topic[0]);
    if(off % n == 0) {
	idx = off / n;
	topic->next_free_topic = head_free_topic;
	head_topic = topic->next_topic;
	head_free_topic = idx;
	fail = 0;
    }
    
    return fail;
}

    
topic_t* iterate_topic(uint32_t *head)
{
    topic_t *t = NULL;
    if(*head != -1) {
	t = &topics[*head];
	*head = t->next_topic;
    }
    return t;
}

static int new_subscriber(subscriber_t **subscriber)
{
    int fail = 1;
    if(head_free_subscriber != -1) {
	*subscriber = &subscribers[head_free_subscriber];
	head_free_subscriber
	    = subscribers[head_free_subscriber].next_free_subscriber;
	fail = 0;
    }
    return fail;
}

static int free_subscriber(subscriber_t *subscriber)
{
    int succ = 0;
    uint32_t n, off, idx;
    off = (subscriber - &subscriber[0]);
    n = sizeof(subscriber[0]);
    if(off % n == 0) {
	idx = off / n;
	subscriber->next_free_subscriber = head_free_subscriber;
	head_free_subscriber = idx;
	succ = 0;
    }
    
    return succ;
}

uint8_t* simple_calloc(uint32_t num_elem, uint32_t elem_sz)
{
    uint8_t* addr = NULL;
    uint8_t sz = num_elem * elem_sz;
    if((pool_idx + sz) < LEN(mem_pool)) {
	addr = &mem_pool[pool_idx];
	pool_idx += sz;
    }
    return addr;
}

void simple_free(uint8_t *addr) {
    return;
}

topic_t* allocate_topic(char *name, uint32_t elem_sz,
			uint32_t num_elem)
{
    topic_t *t = NULL;
    if(!new_topic(&t)) {
	strncpy(t->name, name, LEN(t->name)-1);
	if((t->queue = simple_calloc(num_elem, elem_sz)) != NULL) {
	    t->elem_sz = elem_sz;
	    t->num_elem = num_elem;
	    t->head_ptr = 0;
	    t->pub_count0 = 0;
	    t->pub_count1 = 0;
	}
	else
	    goto free_topic;
    }
    
    goto ret; 
free_topic:
    free_topic(t);
    t = NULL;
ret:
    return t;
}

subscriber_t* allocate_subscriber(char *name, reset_pos_t rst_pos)
{
    subscriber_t *s = NULL;
    topic_t *t;
    uint32_t iter;
    if(!new_subscriber(&s)) {
	iter = head_topic;
	while((t = iterate_topic(&iter)) != NULL) {
	    if(!strncmp(t->name, name, LEN(t->name))) {
		s->topic_ptr = t;
		s->tail_ptr = 0;
		s->next_rd_count = 0;
		s->rst_pos = rst_pos;
		break;
	    }
	}
	if(t == NULL)
	    goto free_sub;
    }
    goto ret;
free_sub:
    free_subscriber(s);
    s = NULL;
ret:
    return s;
}
#define TOPIC_DATA(t,idx) (t->queue[idx * t->elem_sz])
void pub_data(void *d, topic_t *t)
{
    if(t != NULL) {
	t->pub_count0++;
	__sync_synchronize();
	memcpy(&TOPIC_DATA(t,t->head_ptr), d, t->elem_sz);
	if((t->head_ptr+1) >= t->num_elem) {
	    t->head_ptr = 0;
	}
	else {
	    t->head_ptr++;
	}
	__sync_synchronize();
	t->pub_count1++;
	
    }
}

int poll_data(void *d, subscriber_t *s)
{
    topic_t *t;
    uint64_t diff0, diff1;
    int fail = 1;
    if(s != NULL) {
	t = s->topic_ptr;
    get_data:
	diff1 = t->pub_count1 - s->next_rd_count;
	__sync_synchronize();
	if(diff1 <= t->num_elem) {
	    if(diff1 > 0) {
		memcpy(d, &TOPIC_DATA(t, s->tail_ptr), t->elem_sz);
		__sync_synchronize();
		diff0 = t->pub_count0 - s->next_rd_count;
		if (diff1 <= t->num_elem) {
		    if((diff1 == diff0)) {
			s->tail_ptr++;
			if(s->tail_ptr >= t->num_elem) {
			    s->tail_ptr = 0;
			}
			s->next_rd_count++;
			fail = 0;
		    }
		    else {
			goto get_data;
		    }
		}
		else
		    goto reset_pos;
	    }
	}
	else {
	reset_pos:
	    switch(s->rst_pos) {
	    case RESET_TO_LAST_VALUE:
		do {
		    diff1 = t->pub_count1 - t->num_elem;
		    __sync_synchronize();
		    s->tail_ptr = t->head_ptr;
		    __sync_synchronize();
		    diff0 = t->pub_count0 - t->num_elem;
		} while(diff1 != diff0);
		s->next_rd_count = diff1;
		break;
	    case RESET_TO_FIRST_VALUE:
		do {
		    diff1 = t->pub_count1 - 1;
		    __sync_synchronize();
		    s->tail_ptr = t->head_ptr;
		    __sync_synchronize();
		    diff0 = t->pub_count0 - 1;
		}
		while(diff1 != diff0);
		s->next_rd_count = diff1;
		if(s->tail_ptr == 0)
		    s->tail_ptr = t->num_elem - 1;
		else
		    s->tail_ptr -= 1;
		break;
	    default:
	    case RESET_TO_NEW_VALUE:
		do {
		    diff1 = t->pub_count1;
		    __sync_synchronize();
		    s->tail_ptr = t->head_ptr;
		    __sync_synchronize();
		    diff0 = t->pub_count0;
		}
		while(diff1 != diff0);
		s->next_rd_count = diff1;

		s->next_rd_count = t->pub_count1;
		s->tail_ptr = t->head_ptr;
		break;
	    };
	    goto get_data;
	}
    }
    return fail;
}

void list_all_topics()
{
    uint32_t iter;
    topic_t *t;
    iter = head_topic;
    while((t = iterate_topic(&iter)) != NULL) {
	printf("%s\n", t->name);
    }   
}
