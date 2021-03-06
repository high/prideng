#ifndef __EV_QUEUE_H__
#define __EV_QUEUE_H__

#include <pthread.h>

#define EV_QUEUE_SIZE 10

typedef struct 
{
	char* 				q_data[EV_QUEUE_SIZE];
	int 				head,
						tail;
	
	/* Mutex for making sure that two proccesses can't modify the queue at
	 * the same time 
	 */		
	pthread_mutex_t 	write_mutex;
	
	/* Signal variables that gets triggered when there is a event in the queue */
	pthread_mutex_t 	sig_mutex;
	pthread_cond_t		sig_cond;
	
	/* The number of items in the queue */
	size_t				size;
	
} ev_queue_t;

void
ev_queue_init( ev_queue_t *q );

void 
ev_queue_listen( ev_queue_t *q );
	
void
ev_queue_push( ev_queue_t *q, char *data );

char *
ev_queue_pop( ev_queue_t *q );

#endif
