#include "common.h"


struct sockaddr_in6 addr;
pthread_t completion_id, mem_ctrl_id;
int nofity_number, shut_down;

map<int,string>msp;

extern int recv_package, send_package_ack;
extern double send_time, recv_time, cmt_time;
extern struct target_rate polling;

void *completion_backup(void *);
void (*commit)( struct request_backup *request );
void notify( struct request_backup *request );
void *memory_ctrl_backup();

// int on_event(struct rdma_cm_event *event, int tid)
// {
	// int r = 0;
	// if (event->event == RDMA_CM_EVENT_CONNECT_REQUEST)
	  // r = on_connect_request(event->id, tid);
	// else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
	  // r = on_connection(event->id, tid);
	// // else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
	  // // r = on_disconnect(event->id);
	// else
	  // die("on_event: unknown event.");

	// return r;
// }

struct mythread
{
	pthread_t t;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int req_count;
	double avglat;
}Thread[100];

int tmp[100];

struct rdma_management rrdma[10];

void *polling_fasst(void *f)
{
	int id = *(int *)f;
	printf("polling begin %d\n", id);
	struct rdma_management *rdma = &rrdma[id];
	
	for( int i = 0; i < 10; i ++ ){
		struct ibv_recv_wr wr, *bad_wr = NULL;
		struct ibv_sge sge;
		wr.wr_id = i;
		wr.next = NULL;
		wr.sg_list = &sge;
		wr.num_sge = 1;
		
		sge.addr = (uintptr_t)rdma->memgt->recv_buffer+request_size*i;
		sge.length = request_size;
		sge.lkey = rdma->memgt->recv_mr->lkey;
		
		TEST_NZ(ibv_post_recv(rdma->qpmgt->qp[0], &wr, &bad_wr));
	}
	struct ibv_cq *cq;
	struct ibv_wc *wc, *wc_array; 
	wc_array = ( struct ibv_wc * )malloc( sizeof(struct ibv_wc)*20 );
	cq = rdma->s_ctx->cq_data[0];
	while(!shut_down){
		int num = ibv_poll_cq(cq, 10, wc_array);
		if( num < 0 ) continue;
		for( int k = 0; k < num; k ++ ){
			wc = &wc_array[k];
			if( wc->opcode == IBV_WC_RECV ){
				if( wc->status != IBV_WC_SUCCESS ){
					printf("recv error %d!\n", id);
					
				}
				struct ibv_recv_wr wr, *bad_wr = NULL;
				struct ibv_send_wr swr, *sbad_wr = NULL;
				struct ibv_sge sge;
				// recv a req, first send
				swr.wr_id = 0;
				swr.opcode = IBV_WR_SEND;
				swr.sg_list = &sge;
				swr.send_flags = IBV_SEND_SIGNALED;
				swr.num_sge = 1;
				
				sge.addr = (uintptr_t)rdma->memgt->send_buffer;
				sge.length = request_size;
				sge.lkey = rdma->memgt->send_mr->lkey;
				
				TEST_NZ(ibv_post_send(rdma->qpmgt->qp[0], &swr, &sbad_wr));
				
				// post recv again
				memset(&wr, 0, sizeof(wr));
				wr.wr_id = wc->wr_id;
				wr.next = NULL;
				wr.sg_list = &sge;
				wr.num_sge = 1;
				
				sge.addr = (uintptr_t)rdma->memgt->recv_buffer+request_size*wc->wr_id;
				sge.length = request_size;
				sge.lkey = rdma->memgt->recv_mr->lkey;
				
				TEST_NZ(ibv_post_recv(rdma->qpmgt->qp[0], &wr, &bad_wr));
			}
		}
	}
	
	return NULL;
}

void *polling_pilaf( void *f )
{
	while(!shut_down);
	return NULL;
}


void initialize_backup( void *address, int length, int id )
{
	printf("init %d\n", id);
	struct rdma_management *rdma = &rrdma[id];
	end = 1;
	nofity_number = 0;
	int port = 0;
	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_port  = htons(bind_port);
	rdma->memgt = ( struct memory_management * ) malloc( sizeof( struct memory_management ) );
	rdma->qpmgt = ( struct qp_management * ) malloc( sizeof( struct qp_management ) );
	rdma->memgt->application.next = NULL;
	rdma->memgt->application.address = address;
	rdma->memgt->application.length = length;
	
	struct ibv_wc wc;
	int i = 0, j;
	resources_create( NULL, rdma, id );
	
	memcpy( rdma->memgt->send_buffer, rdma->memgt->rdma_recv_mr, sizeof(struct ibv_mr) );

	post_send( rdma, 0, 50, 0, sizeof(struct ibv_mr), 0 );
	int ss = get_wc( rdma, &wc );
	
	printf("add: %p length: %d\n", rdma->memgt->rdma_recv_mr->addr,
	rdma->memgt->rdma_recv_mr->length);
	
	post_recv( rdma, 0, 20, 0, sizeof(struct ibv_mr));
	ss = get_wc( rdma, &wc );
	memcpy( &rdma->memgt->peer_mr, rdma->memgt->recv_buffer, sizeof(struct ibv_mr) );
	printf("peer add: %p length: %d\n", rdma->memgt->peer_mr.addr,
	rdma->memgt->peer_mr.length);
	
	bind_port ++;

	if( rpc_type == 0 )
	    pthread_create( &Thread[id].t, NULL, polling_fasst, &tmp[id] );
    if( rpc_type == 1 )
	    pthread_create( &Thread[id].t, NULL, polling_pilaf, &tmp[id] );
}

void finalize_backup(struct rdma_management *rdma)
{
	fprintf(stderr, "finalize begin\n");
	shut_down = 1;
	
	/* destroy qp management */
	destroy_qp_management(rdma);
	fprintf(stderr, "destroy qp management success\n");
	
	/* destroy memory management */
	destroy_memory_management(rdma,end);
	fprintf(stderr, "destroy memory management success\n");
		
	/* destroy connection struct */
	destroy_connection(rdma);
	fprintf(stderr, "destroy connection success\n");
	
	fprintf(stderr, "finalize end\n");
}

int main( int argc, char **argv )
{
    //ib_gid = atoi(argv[1]);
    get_args();

    struct ScatterList SL;
	
	for( int i = 0; i < thread_number; i ++ ){
		tmp[i] = i;
		SL.address = ( void * )malloc( RDMA_BUFFER_SIZE );
		SL.length = RDMA_BUFFER_SIZE;
		initialize_backup( SL.address, SL.length, i );
	}
	int x;
	scanf("%d", &x);
	
	for( int i = 0; i < thread_number; i ++ ){
		finalize_backup(&rrdma[i]);
	}
}
