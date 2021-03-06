#include "cs.h"
#include "prop.h"
#include "png.h"
#include "network.h"
#include "receiver.h"
#include "resolve.h"
#include "imdb.h"
#include "object.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>
#include <unistd.h>

void 
png_show_prompt();

int
png_handle_cmd( png_t *png, char *cmd );

int 
png_handle_args( png_t *png, int argc, char **argv );

int main( int argc, char **argv )
{
	char 			cmd[40];
	int 			res;
	int				lsock;
	int 			it;
	pthread_t 		p_thread,
					r_thread,
					re_thread;
	obj_t			obj;
	method_list_t 	m_list;
	cs_t			*cs;
	

	/* Signal for when a conflict resolution is needed */
	pthread_mutex_t resolve_sig_lock;
	pthread_cond_t resolve_sig;

	/* Signal that happends when conflict set 
	 * needs to propagate a generation 
	 */
	pthread_cond_t prop_signal;
	pthread_mutex_t prop_sig_lock;

	pthread_cond_init( &prop_signal, NULL );
	pthread_mutex_init( &prop_sig_lock, NULL );

	pthread_cond_init( &resolve_sig, NULL );
	pthread_mutex_init( &resolve_sig_lock, NULL );
	
	
	method_list_init( &m_list );
	method_list_insert( &m_list, "obj_inc", &obj_inc_res );
	
	dboid_gen( "Object", obj.dboid );
	
	/* Create confligt set that will be used in
	 * the application 
	 */
	/*__conf.cs 				= cs_new( 10, 2 ); */
	
	/* Enable the queues */
	ev_queue_init( &__conf.prop_queue );
	ev_queue_init( &__conf.res_queue );
	
	cs = cs_new( 10, 2, &__conf.prop_queue, &__conf.res_queue );
	
	dboid_gen( "Object", cs->dboid );
		
	
	__conf.prop_sig 		= &prop_signal;
	__conf.prop_sig_lock 	= &prop_sig_lock;
	__conf.resolve_sig 		= &resolve_sig;
	__conf.resolve_sig_lock	= &resolve_sig_lock;
	__conf.m_list			= &m_list;
	
	h_table_init( &__conf.timers, 10, sizeof( struct timeval ) );
	h_table_init( &__conf.cs_list, 10, sizeof( cs_t ) );
	
	rep_list_init( &__conf.rlist );
	imdb_init( &__conf.stable_db );
	
	imdb_store( &__conf.stable_db, "obj", &obj, sizeof( obj_t ) );
	
	/* Add timer for the dboid */
	h_table_insert( &__conf.timers, cs->dboid, malloc( sizeof( struct timeval ) * 10 ) );
	
	png_handle_args( &__conf, argc, argv );

	if( __conf.id == -1 )
	{
		printf( "You must set a replica id!\n" );
		exit( 1 );
	}

	if( __conf.lport != -1 )
	{
		/* Create a TCP server socket */
		lsock = net_create_tcp_server( __conf.lport );
		__conf.lsock = lsock;
	}
	else
	{
		printf( "You must set a listen port!\n" );
		exit( 1 );
	}

	/* Create threads for propagation, 
	 * receving and resolving  
	 */
	pthread_create( &p_thread, NULL, prop_thread, &__conf );
	pthread_create( &r_thread, NULL, receiver_thread, &__conf );
	pthread_create( &re_thread, NULL, resolve_thread, &__conf );

	for( it = 0; it < __conf.rlist.size; it++ )
	{
		int rep_sock;
		rep_t *rep;

		rep = &__conf.rlist.reps[it];
		while( 1 )
		{
			/* Connect to the replica */
			rep_sock = net_create_tcp_socket( rep->host, rep->port );

			if(	rep_sock == -1 )
			{
				printf( "Failed to connect to replica on %s:%d\n", rep->host, rep->port );
				sleep( 1 );

			}
			else
			{
				rep->sock = rep_sock;
				break;
			}
		}
	}
	
	/* Start taking in user commands */
	while( 1 )
	{

		png_show_prompt();

		/* Get the command from the prompt */
		fgets( cmd, 40, stdin );
		cmd[ strlen( cmd ) -1 ] = '\0';

		res = png_handle_cmd( &__conf, cmd );

		/* Check if the quit command was issued */
		if( res == 0 )
		{
			break;
		}

	}

	/* Stops the propagation thread */
	pthread_cancel( p_thread );
	pthread_join( p_thread, NULL );

	pthread_cond_destroy( &prop_signal );
	pthread_mutex_destroy( &prop_sig_lock );
	
	pthread_cond_destroy( &resolve_sig );
	pthread_mutex_destroy( &resolve_sig_lock );
	
	cs_free( __conf.cs );
	rep_list_free( &__conf.rlist );
	h_table_free( &__conf.cs_list );	
	method_list_free( &m_list );
	imdb_close( &__conf.stable_db );
	return 0;
}

void png_show_prompt()
{
	printf( "Command> ");
}

int png_handle_cmd( png_t *png,  char *cmd )
{
	char *curr_cmd;
	
	curr_cmd = strtok( cmd, " " );
		
	if( curr_cmd == NULL )
		return 1;

	/* Check what command that was issued */
	if( strcmp( curr_cmd, "q" ) == 0 ) 
	{
		printf( "Quiting..\n" );
		return 0;
	}
	
	/* Add object and conflict set to the system */
	else if( strcmp( curr_cmd, "addo" ) == 0 )
	{
		cs_t *cs;
		char *dboid;
		char *name;
		
		
		/* Fetch the object name */
		name = strtok( NULL, " " );
		
		/* Create the database id */
		dboid_gen( name, &dboid );
		
		
		
		/* Create a new conflict set for the object */
		cs = cs_new( 10, 2, &png->prop_queue, &png->res_queue );
		strcpy( cs->dboid, dboid );
		
		if( h_table_find( &png->cs_list, dboid ) == NULL )
		{
			h_table_insert( &png->cs_list, dboid, cs );
			printf( "Created object with ID: %s\n", dboid );
		}
		else
		{	
			h_table_remove( &png->cs_list, dboid );
			h_table_insert( &png->cs_list, dboid, cs );
			printf( "Created and removed object with ID: %s\n", dboid );
		}
		
		
		return 1;
	}
	
	else if( strcmp( curr_cmd, "add" ) == 0 )
	{
		int num_updates, it;
		char *updates;
		char *dboid;
		char *name;
		cs_t *cs;
		
		name = strtok( NULL, " " );
		dboid_gen( name, &dboid );
		
		cs = h_table_find( &png->cs_list, dboid ); 
		
		if( cs == NULL )
		{
			printf( "Conflict set with id %s is not found!", dboid );
			return 1;
		}
		
		updates = strtok( NULL, " " );
		if( updates != NULL)
			num_updates = atoi( updates );
		else
			num_updates = 1;
		
		
		
		/*	
		cs = cs_create_trans_obj( png->cs );
		*/
		for( it = 0; it < num_updates; it++ )
		{
			mc_t update;
			
			strcpy( update.dboid, dboid );

			strcpy(update.method_name, "obj_inc" );
			update.num_param 				= 1;
			update.params[0].type 			= TYPE_INT;
			update.params[0].data.int_data 	= 1;
				
			cs_lock( cs );
			
			if( cs_insert( cs, &update, png->id ) == -1 )
			{
				printf( "Conflict set is full!\n" );
				break;
			}
			
			cs_unlock( cs );
		}
		
		cs_unlock( cs );
		
		return 1;
	}
	else if( strcmp( curr_cmd, "prop") == 0 )
	{
		pthread_mutex_lock( png->prop_sig_lock );
		pthread_cond_signal( png->prop_sig );
		pthread_mutex_unlock( png->prop_sig_lock );

		return 1;

	}
	else if( strcmp( curr_cmd, "s") == 0 )
	{
		cs_show( png->cs );

		return 1;
	}
	
	else if( strcmp( curr_cmd, "rep" ) == 0 )
	{
		rep_t rep;
		char *host;
		int id;
		int port;
	
		id = atoi( strtok( NULL, " " ) );
		host = strtok( NULL, " " );
		port = atoi( strtok( NULL, " " ) );
	
		rep.id = id;
		strncpy( rep.host, host, strlen( host ) + 1 );
		rep.port = port;

		/* 
		 * Reseting the socket so that we know that there
		 * is no connection at first
		 */
		rep.sock = -1;
	
		
		/* Add the replica to the replica list */
		rep_list_add( &png->rlist, &rep );
		
		printf( "Replica %d:%s:%d was added\n", rep.id, rep.host, rep.port );

	
		return 1;
	}
	
	else if( strcmp( curr_cmd, "stab") == 0)
	{	
		pthread_mutex_lock( png->resolve_sig_lock );	
		pthread_cond_signal( png->resolve_sig );
		pthread_mutex_unlock( png->resolve_sig_lock );
		
		return 1;
	}
	
	else if( strcmp( curr_cmd, "id" ) == 0 )
	{
		png->id = atoi( strtok( NULL, " " ) );
		return 1;
	}
	
	else if( strcmp( curr_cmd, "lport" ) == 0 )
	{
		png->lport = atoi( strtok( NULL, " " ) );
		return 1;
	}
	
	else
	{
		return -1;
	}
}

int 
png_handle_args(png_t *png, int argc, char **argv )
{
	char *filename, line[50];
	FILE *fp;
	int arg;
	
	png->lport = -1;
	png->id = -1;
	png->lsock = -1;

	while( ( arg = getopt( argc, argv, "c:l:r:") ) != -1 )
	{
		switch( arg )
		{
			case 'c':
			{
				filename = optarg;

				printf( "Reading data from file %s\n", filename );
				
				fp = fopen( filename, "r" );
				while( fgets( line, 50, fp ) )
				{

					png_handle_cmd( png, line );
				}

				fclose( fp );
			}
			break;
			
			case 'r':
				png->id = atoi( optarg );
			break;

			case 'l':
				
				printf( "Listens for connections on port %s\n", optarg );
				png->lport = atoi( optarg );
			break;
		}
	}
	
	return 1;
	
}

