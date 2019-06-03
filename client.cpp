#include "common.h"

struct thread_pool
{
	int number, tmp[10], shutdown;
	pthread_cond_t cond0[10], cond1[10];
	pthread_t completion_id, pthread_id[10], mem_ctrl_id;
	pthread_mutex_t qp_count_mutex[10];
	pthread_cond_t qp_cond0[10], qp_cond1[10];
};

struct rdma_addrinfo *addr;
struct thread_pool *thpl;
int send_package_count, write_count, request_count, send_token;
struct timeval test_start;
extern double get_working, do_working, \
cq_send, cq_recv, cq_write, cq_waiting, cq_poll, q_task, other,\
send_package_time, end_time, base, working_write, q_qp,\
init_remote, init_scatter, q_scatter, one_rq_end, one_rq_start,\
sum_tran, sbf_time, callback_time, get_request;
extern int d_count, send_new_id, rq_sub, mx, q_count;
extern struct target_rate polling_active;

void initialize_active( void *address, int length, char *ip_address, struct rdma_management *rdma, int id );
int on_event(struct rdma_cm_event *event, int tid);
void *working_thread(void *arg);
void *completion_active();
void huawei_send( struct request_active *rq );
void *full_time_send();
int clean_send_buffer( struct package_active *now );
int clean_package( struct package_active *now );
void *memory_ctrl_active();

void initialize_active( void *address, int length, char *ip_address, struct rdma_management *rdma, int id )
{
	end = 0;
	rdma->memgt = ( struct memory_management * ) malloc( sizeof( struct memory_management ) );
	rdma->qpmgt = ( struct qp_management * ) malloc( sizeof( struct qp_management ) );
	rdma->memgt->application.next = NULL;
	rdma->memgt->application.address = address;
	rdma->memgt->application.length = length;
	
	/*建立连接，数量为connect_number，\
	每次通过第0条链路传输port及ibv_mr，第0条链路端口固定为bind_port*/
	struct ibv_wc wc;
	resources_create( ip_address, rdma, id );
	
	post_recv( rdma, 0, 20, 0, sizeof(struct ibv_mr));
	int tmp = get_wc( rdma, &wc );
	
	memcpy( &rdma->memgt->peer_mr, rdma->memgt->recv_buffer, sizeof(struct ibv_mr) );
	printf("peer add: %p length: %d\n", rdma->memgt->peer_mr.addr,
	rdma->memgt->peer_mr.length);
	
	memcpy( rdma->memgt->send_buffer, rdma->memgt->rdma_recv_mr, sizeof(struct ibv_mr) );
	post_send( rdma, 0, 50, 0, sizeof(struct ibv_mr), 0 );
	
	int ss = get_wc( rdma, &wc );
	printf("add: %p length: %d\n", rdma->memgt->rdma_recv_mr->addr,
	rdma->memgt->rdma_recv_mr->length);

	fprintf(stderr, "create pthread pool end\n");
	
	bind_port ++;
}

void finalize_active( struct rdma_management *rdma )
{
	/* destroy qp management */
	destroy_qp_management(rdma);
	fprintf(stderr, "destroy qp management success\n");
	
	/* destroy memory management */
	destroy_memory_management(rdma, end);
	fprintf(stderr, "destroy memory management success\n");
		
	/* destroy connection struct */
	destroy_connection(rdma);
	fprintf(stderr, "destroy connection success\n");
	fprintf(stderr, "finalize end\n");
}

struct rdma_management rrdma[100];

struct mythread
{
	pthread_t t;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int req_count;
	double avglat;
}Thread[100];

pthread_cond_t share_cond;

int tmp[100];

void *working_fasst( void *f )
{
	int id = *(int *)f;
	printf("testing thread %d ready\n", id);
	struct rdma_management *rdma = &rrdma[id];
	struct mythread *my = &Thread[id];
	pthread_mutex_init( &my->mutex, NULL );
	pthread_cond_init( &my->cond, NULL );
	pthread_mutex_lock( &my->mutex );
	pthread_cond_wait( &my->cond, &my->mutex );
	pthread_mutex_unlock( &my->mutex );
	printf("testing thread %d begin\n", id);
	
	double sum = 0.0;
	int count = test_count_per_thread;
	
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
	
	double st = elapse_sec(), ed;
	for( int i = 0; i < count; i ++ ){
		//send a req
		struct ibv_send_wr wr, *bad_wr = NULL;
		struct ibv_sge sge;
		memset(&wr, 0, sizeof(wr));
		wr.wr_id = 0;
		wr.opcode = IBV_WR_SEND;
		wr.sg_list = &sge;
		wr.send_flags = IBV_SEND_SIGNALED;
		wr.num_sge = 1;
		
		sge.addr = (uintptr_t)rdma->memgt->send_buffer;
		sge.length = request_size;
		sge.lkey = rdma->memgt->send_mr->lkey;
		
		TEST_NZ(ibv_post_send(rdma->qpmgt->qp[0], &wr, &bad_wr));
		
		//polling for a recv
		struct ibv_cq *cq;
		struct ibv_wc *wc, *wc_array; 
		wc_array = ( struct ibv_wc * )malloc( sizeof(struct ibv_wc)*20 );
		int fl = 0;
		cq = rdma->s_ctx->cq_data[0];
		while(!fl){
			int num = ibv_poll_cq(cq, 10, wc_array);
			if( num < 0 ) continue;
			for( int k = 0; k < num; k ++ ){
				wc = &wc_array[k];
				if( wc->opcode == IBV_WC_RECV ){
					if( wc->status != IBV_WC_SUCCESS ){
						printf("recv error %d!\n", id);
						
					}
					fl = 1;
					struct ibv_recv_wr wr, *bad_wr = NULL;
					struct ibv_sge sge;
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
		ed = elapse_sec();
		//printf("req %d %.2f %.2f\n", i, ed, st);
		sum += ed-st;
		st = ed;
	}
	my->avglat = sum/test_count_per_thread;
	printf("test thread %d over avg %.2fus\n", id, my->avglat);
	return NULL;
}

void *working_pilaf( void *f )
{
	int id = *(int *)f;
	printf("testing thread %d ready\n", id);
	struct rdma_management *rdma = &rrdma[id];
	struct mythread *my = &Thread[id];
	pthread_mutex_init( &my->mutex, NULL );
	pthread_cond_init( &my->cond, NULL );
	pthread_mutex_lock( &my->mutex );
	pthread_cond_wait( &my->cond, &my->mutex );
	pthread_mutex_unlock( &my->mutex );
	printf("testing thread %d begin\n", id);
	
	double sum = 0.0;
	int count = test_count_per_thread;
	double st = elapse_sec(), ed;
		
	for( int i = 0; i < count; i ++ ){
		//read key
		struct ibv_send_wr wr, *bad_wr = NULL;
		struct ibv_sge sge;
		memset(&wr, 0, sizeof(wr));
		wr.wr_id = 2*i;
		wr.opcode = IBV_WR_RDMA_READ;
		wr.sg_list = &sge;
		wr.send_flags = IBV_SEND_SIGNALED;
		wr.num_sge = 1;
		wr.wr.rdma.remote_addr = (uintptr_t)rdma->memgt->peer_mr.addr;
		wr.wr.rdma.rkey = rdma->memgt->peer_mr.rkey;
		
		sge.addr = (uintptr_t)rdma->memgt->rdma_recv_mr->addr;
		sge.length = request_size;
		sge.lkey = rdma->memgt->rdma_recv_mr->lkey;
		
		TEST_NZ(ibv_post_send(rdma->qpmgt->qp[0], &wr, &bad_wr));
		
		//polling for a read CQ
		struct ibv_cq *cq;
		struct ibv_wc *wc, *wc_array; 
		wc_array = ( struct ibv_wc * )malloc( sizeof(struct ibv_wc)*20 );
		int fl = 0;
		cq = rdma->s_ctx->cq_data[0];
		while(!fl){
			int num = ibv_poll_cq(cq, 10, wc_array);
			if( num < 0 ) continue;
			for( int k = 0; k < num; k ++ ){
                //printf("polling %d\n", num);
			    wc = &wc_array[k];
				//printf("%d %d\n", wc->opcode, wc->wr_id );
			    if( wc->opcode == IBV_WC_RDMA_READ ){
					if( wc->status != IBV_WC_SUCCESS ){
						printf("recv error %d!\n", id);
					}
                    //printf("xxx");
					fl = 1;
                    break;
				}
			}
		}

        memset(&wr, 0, sizeof(wr));
        wr.wr_id = 2*i+1;
        wr.opcode = IBV_WR_RDMA_READ;
        wr.sg_list = &sge;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.num_sge = 1;
        wr.wr.rdma.remote_addr = (uintptr_t)rdma->memgt->peer_mr.addr+request_size;
        wr.wr.rdma.rkey = rdma->memgt->peer_mr.rkey;

        sge.addr = (uintptr_t)rdma->memgt->rdma_recv_mr->addr;
        sge.length = request_size;
        sge.lkey = rdma->memgt->rdma_recv_mr->lkey;

        TEST_NZ(ibv_post_send(rdma->qpmgt->qp[0], &wr, &bad_wr));

        //printf("polling\n");
        //polling for a read CQ
        // struct ibv_cq *cq;
        // struct ibv_wc *wc, *wc_array;
        wc_array = ( struct ibv_wc * )malloc( sizeof(struct ibv_wc)*20 );
        fl = 0;
        cq = rdma->s_ctx->cq_data[0];
        while(!fl){
            int num = ibv_poll_cq(cq, 10, wc_array);
            if( num < 0 ) continue;
            for( int k = 0; k < num; k ++ ){
                //printf("polling %d\n", num);
                wc = &wc_array[k];
                if( wc->opcode == IBV_WC_RDMA_READ ){
                    if( wc->status != IBV_WC_SUCCESS ){
                        printf("recv error %d!\n", id);
                    }
                    fl = 1;
                    break;
                }
            }
        }

        ed = elapse_sec();
        sum += ed-st;
        st = ed;
	}
	my->avglat = sum/test_count_per_thread;
	printf("test thread %d over avg %.2fus\n", id, my->avglat);
	return NULL;
}

int main( int argc, char **argv )
{
	struct ScatterList SL;
	if( argc != 2 ){
		printf("error input\n");
		exit(1);
	}
    //ib_gid = atoi(argv[2]);
    get_args();

	//pthread_cond_init( &share_cond, NULL );
	for( int i = 0; i < thread_number; i ++ ){
		SL.address = ( void * )malloc( RDMA_BUFFER_SIZE );
		SL.length = RDMA_BUFFER_SIZE;
		initialize_active( SL.address, SL.length, argv[1], &rrdma[i], i );
		tmp[i] = i;
		if( rpc_type == 0 )
		    pthread_create( &Thread[i].t, NULL, working_fasst, (void*)&tmp[i] );
        if( rpc_type == 1 )
		    pthread_create( &Thread[i].t, NULL, working_pilaf, (void*)&tmp[i] );
	}

	sleep(1);
	printf("begin notify all\n");
	for( int i = 0; i < thread_number; i ++ ){
	    pthread_cond_signal( &Thread[i].cond );
	}
	double st = elapse_sec(), ed;
	double tot = 0.0;
	for( int i = 0; i < thread_number; i ++ ){
		pthread_join( Thread[i].t, NULL );
	}
	ed = elapse_sec();
	
	for( int i = 0; i < thread_number; i ++ ){
		finalize_active( &rrdma[i] );
		tot += Thread[i].avglat;
	}
	
	tot /= thread_number;
	int num = thread_number*test_count_per_thread;
	printf("test req %d IOPS %.2fk/s latency %.6fus\n",
	num, num*1.0/(ed-st)*1e3, tot);
}