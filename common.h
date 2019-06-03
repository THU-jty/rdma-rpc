#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#define _GNU_SOURCE
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>  
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>  
#include <sys/stat.h>
#include <sys/prctl.h>
#include <map>
#include <iostream>
#include <fstream>
using namespace std;

//#define __DEBUG
//#define __OUT
//#define __MUTEX
//#define _TEST_SYN
#define __TIMEOUT_SEND
//#define __BITMAP
//#define __STRONG_FLOW_CONTROL
#define __polling
//#define __resub

#ifdef __DEBUG
#define DEBUG(info,...)    printf(info, ##__VA_ARGS__)
#else
#define DEBUG(info,...)
#endif

#ifdef __OUT
#define OUT(info,...)    printf(info, ##__VA_ARGS__)
#else
#define OUT(info,...)
#endif

#ifdef __resub
#define resub(info,...)    printf(info, ##__VA_ARGS__)
#else
#define resub(info,...)
#endif

#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)
#define TIMEOUT_IN_MS 500
typedef unsigned int uint;
typedef unsigned long long ull;
typedef unsigned char uchar;

struct ScatterList
{
	struct ScatterList *next;  //链表的形式实现
	void *address;             //数据存放区域的页内地址
	int length;                //数据存放区域的长度
};

/* structure to exchange data which is needed to connect the QPs */
struct cm_con_data_t {
	uint32_t 			qp_num;		/* QP number */
	uint16_t 			lid;		/* LID of the IB port */
	uint8_t     		remoteGid[16];  /* GID  */
};

struct connection
{
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_cq **cq_data, **cq_ctrl, **cq_mem;
	struct ibv_comp_channel *comp_channel, *mem_channel;
	struct ibv_port_attr		port_attr;	
	int gidIndex;
	union ibv_gid gid;
};

struct memory_management
{
	int number;
	int memory_reback_flag;
	
	struct ScatterList application;
	
	struct ibv_mr *rdma_send_mr;
	struct ibv_mr *rdma_recv_mr;
	
	struct ibv_mr *send_mr;
	struct ibv_mr *recv_mr;
	
	struct ibv_mr peer_mr;
	
	char *recv_buffer;
	char *send_buffer;
	
	char *rdma_send_region;
	char *rdma_recv_region;	
	
	struct bitmap *send, *peer[10], *peer_using[10], *peer_used[10];
	void **mapping_table[10];
	
	pthread_mutex_t mapping_mutex[10], flag_mutex;
};

struct qp_management
{
	int data_num;
	int ctrl_num;
	struct ibv_qp *qp[128];
};

struct rdma_management
{
	struct connection *s_ctx;
	struct memory_management *memgt;
	struct qp_management *qpmgt;
};


extern struct rdma_cm_event *event;
extern struct rdma_event_channel *ec;
extern struct rdma_cm_id *conn_id[128], *listener[128];
extern int end;//active 0 backup 1
/* both */
extern int bind_port;
extern int BUFFER_SIZE;
extern int TOTAL_SIZE;
extern int BUFFER_SIZE_EXTEND;
extern int RDMA_BUFFER_SIZE;
extern int connect_number;
extern int buffer_per_size;
extern int ctrl_number;
extern int full_time_interval;
extern int recv_buffer_num;//主从两端每个qp控制数据缓冲区个数
extern int package_pool_size;
extern int cq_ctrl_num;
extern int cq_data_num;
extern int cq_size;
extern int qp_size;
extern int qp_size_limit;
extern int concurrency_num;
extern int memory_reback_number;
extern ull magic_number;
extern int ib_gid;
extern int test_count_per_thread;
extern int thread_number;
extern int rpc_type;
/* active */
extern int resend_limit;
extern int request_size;
extern int scatter_size;
extern int package_size;
extern int recv_imm_data_num;//主端接收从端imm_data wr个数
extern int request_buffer_size;
extern int scatter_buffer_size;
extern int task_pool_size;
extern int scatter_pool_size;
/* backup */
extern int ScatterList_pool_size;
extern int request_pool_size;

int resources_create(char *ip_address, struct rdma_management *rdma, int id );
void print_GID( uint8_t *a );
int connect_qp(struct rdma_management *rdma, struct ibv_qp *myqp, int id);
void build_connection(struct rdma_management *rdma, int tid);
void build_context(struct connection *s_ctx);
void register_memory( struct memory_management *memgt, struct connection *s_ctx, int tid );
void post_recv( struct rdma_management *rdma, int qp_id, ull tid, int offset, int recv_size);
void post_send( struct rdma_management *rdma, int qp_id, ull tid, int offset, int send_size, int imm_data );
void die(const char *reason);
int get_wc( struct rdma_management *rdma, struct ibv_wc *wc );
int destroy_qp_management( struct rdma_management *rdma );
int destroy_connection( struct rdma_management *rdma );
int destroy_memory_management( struct rdma_management *rdma, int end );
double elapse_sec();
void get_args();

#endif
