#define _GNU_SOURCE

// SPDX-License-Identifier: GPL-2.0-only, 3-clause BSD
/*
 * tnd - Simple Migration Functional 2 Node Test D
 *
 *  Copyright (C) 2020 Narf Industries Inc., Javier Malave <javier.malave@narfindustries.com>
 *
 *
 * Based on code by:
 *   University of Virginia Tech - Popcorn Kernel Library
 *
 * What this file implements:
 *
 * Setup/Config Rules - Linux will be compiled and configured for QEMU with X86 and ARM architectures
 * IP addresses (10.4.4.100, 10.4.4.101) will be used for node 0 and node 1 respectively.
 *
 * Run Description - This test will fork multiple threads and migrate them using popcorn_migrate from node A to node B.
 * Threads will "run" back and forth from between the source node and sink node.
 * This test is to prove pthreads work on heterogeneous Popcorn Linux (kernel v5.2)  so long as they don't exit.
 * Nodes will be of X86 and ARM architecture only.
 * Pass criteria - Popcorn_migrate returns without errors. Threads are successfully migrated to Node B and back to Node A without errors.
 * Task_struct should match expected values at node B & node A.
 * Input/Output - This test takes three inputs. int A = Source Node; int B = Sink Node; int C = Number of Threads
 * Platform - This test must run on QEMU, HW (x86/ARM) -> [TODO]
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
/* POPCORN LIBRARY DECLARATIONS AND DEFINITIONS LOCATED AD /USR/LOCAL/POPCORN/X86_64/INCLUDE */
#include <platform.h>

/**
 * Migrate this thread to node @nid. if @nid == -1, use the proposed
 * migration location.
 *
 * return 0 on success, return non-zero otherwise
 *  EINVAL: invalid migration destination @nid
 *  EAGAIN: @nid is offline now
 *  EBUSY:  already running at @nid
 */
int migrate(int nid, void (*callback)(void *), void *callback_param);

/**
 * Frequently used configurations
 */
//#ifdef _POPCORN_ALIGN_VARIABLES
//struct __popcorn_config_t {
//	int configs[1024];
//} ALIGN_TO_PAGE;
//#else
//struct __popcorn_config_t {
//	int configs[2];
//};
//#endif /* _POPCORN_ALIGN_VARIABLES */
//
//#define POPCORN_DEFINE_CONFIGS()	\
//	struct __popcorn_config_t __popcorn_configs = { \
//		.configs = {0}, \
//	}
//#define POPCORN_CONFIG_CORES_PER_NODE	__popcorn_config.configs[0]
//#define POPCORN_CONFIG_NODES			__popcorn_config.configs[1]

int popcorn_gettid(void)
{
	return syscall( __NR_gettid);
}

/**
 * Return the tid of the current context. This is a wrapper for
 * syscall(SYS_gettid)
 */
int popcorn_gettid();


/**
 * Get the popcorn node information
 */
/*enum popcorn_node_status {
	POPCORN_NODE_OFFLINE = 0x00,
	POPCORN_NODE_ONLINE = 0x01,
};*/

enum popcorn_arch_types {
	POPCORN_NODE_UNKNOWN = -1,
	POPCORN_NODE_AARCH64 = 0,
	POPCORN_NODE_X86 = 1,
	POPCORN_NODE_PPC64 = 2,
};

#define TND_NODES 32
#define NODE_OFFLINE 0
#define X86_64_ARCH 1
static const char *arch_sz[] = {
	"unknown",
	"arm64",
	"x86-64",
	"ppc64le",
	NULL,
};

struct thread {
	pthread_t thread_info;
	pid_t tid;
	int source_nid;
	int sink_nid;
	int done;
	int err;
	int flag;
	int arch_types[2]; // assume two nodes for now; TBD make this dynamic
};
pthread_barrier_t barrier_start;
struct thread *pcn_thread;
struct thread **threads;
int nthreads;
int all_done;
int gcount;

static int __init_thread_params(void)
{
	int i;
	int init_errno;
	threads = (struct thread **)malloc(sizeof(struct thread *) * nthreads);


	for (i = 0; i < nthreads; i++) {
		if((init_errno = posix_memalign((void **)&(threads[i]), PAGESZ, sizeof(struct thread)))) {
			return init_errno;
		};
	}

	pthread_barrier_init(&barrier_start, NULL, nthreads + 1);
	return 0;
}


int node_sanity_check(int local_nid, int remote_nid, int *arch_types)
{
	int current_nid;
	struct popcorn_node_status pnodes[TND_NODES];
	int node_err = 0;

	/* Get Node Info. Make sure We can retrieve the right node's information */
        node_err = popcorn_getnodeinfo(&current_nid, pnodes); //TND_NODES);

	if (node_err) {
		printf("TND FAILED: popcorn_get_node_info, Cannot retrieve the nodes' information at node %d. ERROR CODE %d\n", current_nid, node_err);
		return node_err;
	}

	/* Testing Node Info */
	if(current_nid != local_nid) {
		printf("TND FAILED: We should be at Node %d. Yet we are at node %d\n", local_nid, current_nid);
		node_err = -1;
		return node_err;
	} else if(pnodes[local_nid].status == NODE_OFFLINE) {
		printf("TND FAILED: Node %d is offline.\n", local_nid);
		node_err = -1;
		return node_err;
	} else if(pnodes[remote_nid].status == NODE_OFFLINE) {
		printf("TND FAILED: Node %d is offline.\n", remote_nid);
		node_err = -1;
		return node_err;
	}

	arch_types[local_nid] = pnodes[local_nid].arch + 1;
	arch_types[remote_nid] = pnodes[remote_nid].arch + 1;

	//printf("FT_2_D: Local Node %d architecture is %s.\n", local_nid, arch_sz[pnodes[local_nid].arch + 1]);
	//printf("FT_2_D: Remote Node %d architecture is %s.\n", remote_nid, arch_sz[pnodes[remote_nid].arch + 1]);

	return node_err;
}

int thread_sanity_check(int nid, pid_t tid)
{
	struct popcorn_thread_status status;
	int thread_err = 0;

    thread_err = popcorn_getthreadinfo(&status);

	if (thread_err) {
		printf("TND FAILED: popcorn_get_status, Cannot retrieve the thread' information at node %d. ERROR CODE: %d\n", nid, thread_err);
		return thread_err;
	}

	if(status.current_nid != nid) {
		printf("TND FAILED: popcorn_get_status, Thread %d should be at node %d. But instead it is at node %d\n", tid, nid, status.current_nid);
		thread_err = -1;
		return thread_err;
	}

	return thread_err;
}

static void *child_thread(void *input)
{
	struct thread *current_thread = input;
	pid_t source_tid = gettid();
	pid_t sink_tid;
	int rnode = current_thread->sink_nid;
	int snode = current_thread->source_nid;

	if(source_tid == -1) {
		printf("TND FAILED: Thread ID is not a positive integer, TID: %d\n", source_tid);
		current_thread->err = -1;
		current_thread->done = 1;
		all_done++;
		return NULL;
	}

	//printf("TND: Thread ID is %d\n", source_tid);
	current_thread->tid = source_tid;

	/* Node Sanity Check */
	if((current_thread->err = node_sanity_check(snode, rnode, current_thread->arch_types))) {
		current_thread->done = 1;
		all_done++;
		return NULL;
	}
//
//	/* Get thread status. */
//	if((current_thread->err = thread_sanity_check(current_thread->source_nid, source_tid))) {
//		current_thread->done = 1;
//		all_done++;
//		return NULL;
//	}

	while(1){


		/* Migrate Thread to sink_nid */
		current_thread->err = migrate(rnode, NULL, NULL);
		if (current_thread->err) {
			switch (current_thread->err) {
				case -EINVAL:
					printf("TND FAILED: Thread %d. Invalid Migration Destination %d\n", source_tid,
					       current_thread->sink_nid);
					current_thread->done = 1;
					break;
				case -EBUSY:
					printf("TND FAILED: Thread %d already running at destination %d\n", source_tid,
					       current_thread->sink_nid);
					current_thread->done = 1;
					break;
				case -EAGAIN:
					printf("TND FAILED: Thread %d could not reach destination %d. Node is offline.\n",
					       source_tid, current_thread->sink_nid);
					current_thread->done = 1;
					break;
				default:
					printf("TND FAILED: Thread %d could not migrate, process_server_do_migration returned %d\n",
					       source_tid, current_thread->err);
					current_thread->done = 1;
			}
		}

		/* Did we fail at the switch statement? */
		if (current_thread->done) {
			all_done++;
			return NULL;
		}

		//printf("TND: We should have arrived at sink node.\n");

		/* We should be at sink_nid. Get TID at Sink Node */
		sink_tid = gettid();
		current_thread->flag = sink_tid;

		if (sink_tid == -1) {
			printf("TND FAILED: Thread ID is not a positive integer, TID: %d\n", sink_tid);
			current_thread->err = -1;
			current_thread->done = 1;
			all_done++;
			return NULL;
		}

		printf("TND[%d]: Touched %s line, pivot and run back to source node\n", sink_tid,
		       arch_sz[current_thread->arch_types[rnode]]);

		/* Node Sanity Check */
//	if((current_thread->err = node_sanity_check(current_thread->sink_nid, current_thread->source_nid))) {
//		current_thread->done = 1;
//		all_done++;
//		return NULL;
//	}

		/* Migrate Thread Back to source_nid */
		current_thread->err = migrate(current_thread->source_nid, NULL, NULL);

		printf("TND[%d]: Touched %s line, pivot and run back to sink node\n", source_tid,
		       arch_sz[current_thread->arch_types[snode]]);

		/*Take some well deserved rest, than do another back and forth*/
		sleep(1);
	}
	return NULL;
}

int main(int argc, char *argv[])
{

#ifdef __x86_64__
    int tnd_errno = 0;
    int source_node;
    int sink_node;
    int i;
    pid_t source_pid = gettid();
    nthreads = 0;
    all_done = 0;
    gcount = 0;

    if(source_pid == -1) {
        printf("TND FAILED: Process ID is not a positive integer, PID: %d\n", source_pid); // NOTE TID (TGID) should be == to PID (TID) in this instance
        tnd_errno = -1;
        return tnd_errno;
    }

	if (argc != 4) {
        printf("TND FAILED: This test takes 3 arguments, Source Node ID, Sink Node ID, # of threads.\n");
    }


	printf("TND: Process ID is %d\n", source_pid);

	source_node = atoi(argv[1]);
	sink_node = atoi(argv[2]);
    	nthreads = atoi(argv[3]);

    if(source_node == sink_node) {
        printf("TND FAILED: Source Node ID must be different to Sink Node ID\n");
        tnd_errno = -1;
        return tnd_errno;
    } else if((source_node | sink_node) < 0 || source_node >= MAX_POPCORN_NODES || sink_node >= MAX_POPCORN_NODES) {
        printf("TND FAILED: Node ID's must be a positive integer 0-31\n");
        tnd_errno = -1;
        return tnd_errno;
    }

    /* Init popcorn "traveling" threads */
    if((tnd_errno = __init_thread_params())){
        printf("TND FAILED: __init_thread_params() failed error %d\n", tnd_errno);
        return tnd_errno;
    }
    for(i = 0; i < nthreads; i++) {
        pcn_thread = threads[i];
        pcn_thread->tid = 0;
        pcn_thread->done = 0;
        pcn_thread->err = 0;
        pcn_thread->source_nid = source_node;
        pcn_thread->sink_nid = sink_node;
        pcn_thread->flag = 0;
        pcn_thread->arch_types[0] = 0; // Begin with unknown arch
        pcn_thread->arch_types[1] = 0; // Begin with unknown arch
        /* Create and init pthread */
        pthread_create(&pcn_thread->thread_info, NULL, &child_thread, pcn_thread);
    }

    pthread_barrier_wait(&barrier_start);

    while(gcount < nthreads);

    pthread_barrier_wait(&barrier_start);

    while(all_done < nthreads);

    for(i = 0; i < nthreads; i++) {
	int *errno_ptr = &tnd_errno;
	pcn_thread = threads[i];
    	pthread_join(pcn_thread->thread_info, (void(**))(&errno_ptr));
    	printf("TND TEST at NODE %d Thread %d exited with CODE %d\n", source_node, pcn_thread->tid, pcn_thread->err);
    	free(pcn_thread);
    }

    printf("TND TEST PASSED at NODE %d\n", source_node);
    return tnd_errno;
#else
	int tnd_errno;
	printf("TND: Test only supports X86_64 Architecture\n");
	tnd_errno = -1;
	return ft2e_errno;
#endif
}