#include "ev_queue.h"

#include <stdio.h>

void
ev_queue_init( ev_queue_t *q )
{
	q->head = -1;
	q->tail = -1;
	q->size = 0;
	pthread_mutex_init( &q->write_mutex, NULL );
	
	pthread_mutex_init( &q->sig_mutex, NULL );
	pthread_cond_init( &q->sig_cond, NULL );
}

void 
ev_queue_listen( ev_queue_t *q )
{
	pthread_mutex_lock( &q->sig_mutex );
	
	/* Check if the queue is empty */
	if( q->size == 0 )
	{
		pthread_cond_wait( &q->sig_cond, &q->sig_mutex );
	}

	pthread_mutex_unlock( &q->sig_mutex );
	
}

void
ev_queue_push(ev_queue_t *q, char *data )
{
	pthread_mutex_lock( &q->write_mutex );
	
	if( q->head == -1 )
	{
		q->head = q->tail = 0;
		q->q_data[ q->head ] = data;
	}
	else
	{
		/* q->tail++; */
		q->tail = (q->tail + 1) % EV_QUEUE_SIZE;
		q->q_data[ q->tail ] = data;
	}
	
	q->size++;
	
	printf( "Adding new event with id: %s\n", data );
	
	pthread_mutex_unlock( &q->write_mutex );
	
	/* Signal to the queue listeners */
	pthread_mutex_lock( &q->sig_mutex );
	pthread_cond_signal( &q->sig_cond );
	pthread_mutex_unlock( &q->sig_mutex );
}

char *
ev_queue_pop( ev_queue_t *q )
{
	char *data;
	
	pthread_mutex_lock( &q->write_mutex );
	
	data = q->q_data[ q->head ];
	
	q->head = (q->head + 1) % EV_QUEUE_SIZE;
	q->size--;
	
	pthread_mutex_unlock( &q->write_mutex );
	
	
	return data;
}

