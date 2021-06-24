#define _GNU_SOURCE

// SPDX-License-Identifier: GPL-2.0-only, 3-clause BSD
/*
 * ft2c - Simple Migration Functional 2 Node Test C
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
 *
 * Run Description - This test will fork a thread and migrate it using popcorn_migrate from node A to node B.
 * The test will be performed on x86 & ARM architectures. Each node will be of a different architecture.
 * Pass criteria - Popcorn_migrate returns without errors. Thread is successfully migrated to Node B and back to Node A without errors.
 * Task_struct should match expected values at node B & node A.
 * Input/Output - This test takes two inputs. int A = Source Node; int B = Sink Node
 * Platform - This test must run on QEMU, HW (x86, ARM) -> [TODO]
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

#define REMOTE_PRINT 0

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

#define FT2C_NODES 32
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
};
pthread_barrier_t barrier_start;
struct thread *pcn_thread;

int node_sanity_check(int local_nid, int remote_nid)
{
    int current_nid;
    struct popcorn_node_status pnodes[FT2C_NODES];
    int node_err = 0;

    /* Get Node Info. Make sure We can retrieve the right node's information */
    node_err = popcorn_getnodeinfo(&current_nid, pnodes); //FT2C_NODES);

    if (node_err)
    {
        printf("FT_2_C FAILED: popcorn_get_node_info, Cannot retrieve the nodes' information at node %d. ERROR CODE %d\n", current_nid, node_err);
        return node_err;
    }

    /* Testing Node Info */
    if(current_nid != local_nid)
    {
        printf("FT_2_C FAILED: We should be at Node %d. Yet we are at node %d\n", local_nid, current_nid);
        node_err = -1;
        return node_err;
    }
    else if(pnodes[local_nid].status == NODE_OFFLINE)
    {
        printf("FT_2_C FAILED: Node %d is offline.\n", local_nid);
        node_err = -1;
        return node_err;
    }
    else if(pnodes[remote_nid].status == NODE_OFFLINE)
    {
        printf("FT_2_C FAILED: Node %d is offline.\n", remote_nid);
        node_err = -1;
        return node_err;
    }

    printf("FT_2_C: Local Node %d architecture is %s.\n", local_nid, arch_sz[pnodes[local_nid].arch + 1]);
    printf("FT_2_C: Remote Node %d architecture is %s.\n", remote_nid, arch_sz[pnodes[remote_nid].arch + 1]);

    return node_err;
}

int thread_sanity_check(int nid, pid_t tid)
{
    struct popcorn_thread_status status;
    int thread_err = 0;

    thread_err = popcorn_getthreadinfo(&status);

    if (thread_err)
    {
        printf("FT_2_C FAILED: popcorn_get_status, Cannot retrieve the thread' information at node %d. ERROR CODE: %d\n", nid, thread_err);
        return thread_err;
    }

    if(status.current_nid != nid)
    {
        printf("FT_2_C FAILED: popcorn_get_status, Thread %d should be at node %d. But instead it is at node %d\n", tid, nid, status.current_nid);
        thread_err = -1;
        return thread_err;
    }

    return thread_err;
}

static void *child_thread(void *input)
{
    int ft2c_thread_errno = 0;
    pid_t source_tid = gettid();
    pid_t sink_tid;
    pid_t temp_tid;
    pthread_barrier_wait(&barrier_start);

    if(source_tid == -1)
    {
        printf("FT_2_C FAILED: Thread ID is not a positive integer, TID: %d\n", source_tid);
        ft2c_thread_errno = -1;
        pcn_thread->done = 1;
        return NULL;
    }

    printf("FT_2_D: Thread ID is %d\n", source_tid);
    pcn_thread->tid = source_tid;

    /* Node Sanity Check */
    if((ft2c_thread_errno = node_sanity_check(pcn_thread->source_nid, pcn_thread->sink_nid)))
    {
        pcn_thread->done = 1;
        return NULL;
    }

    /*Get thread status. */
    if((ft2c_thread_errno = thread_sanity_check(pcn_thread->source_nid, source_tid)))
    {
        pcn_thread->done = 1;
        return NULL;
    }

    /* Migrate Thread to sink_nid */
    ft2c_thread_errno = migrate(pcn_thread->sink_nid, NULL, NULL);
    if(ft2c_thread_errno)
    {
        switch (ft2c_thread_errno) {
            case -EINVAL:
                printf("FT_2_C FAILED: Thread %d. Invalid Migration Destination %d\n", source_tid, pcn_thread->sink_nid);
                pcn_thread->done = 1;
                break;
            case -EBUSY:
                printf("FT_2_C FAILED: Thread %d already running at destination %d\n", source_tid, pcn_thread->sink_nid);
                pcn_thread->done = 1;
                break;
            case -EAGAIN:
                printf("FT_2_C FAILED: Thread %d could not reach destination %d. Node is offline.\n", source_tid, pcn_thread->sink_nid);
                pcn_thread->done = 1;
                break;
            default:
                printf("FT_2_C FAILED: Thread %d could not migrate, process_server_do_migration returned %d\n", source_tid, ft2c_thread_errno);
                pcn_thread->done = 1;
        }
    }

    /* Did we fail at the switch statement? */
    if(pcn_thread->done)
    {
        return NULL;
    }

#if REMOTE_PRINT
    printf("FT_2_C: We should have arrived at sink node.\n");
#endif

    /* We should be at sink_nid. Get TID at Sink Node */
    sink_tid = gettid();

    if(sink_tid == -1)
    {
        printf("FT_2_C FAILED: Thread ID is not a positive integer, TID: %d\n", sink_tid);
        ft2c_thread_errno = -1;
        pcn_thread->done = 1;
        return NULL;
    }

#if REMOTE_PRINT
    printf("FT_2_C: Thread ID is %d\n", sink_tid);

    /* Node Sanity Check */
    if((ft2c_thread_errno = node_sanity_check(pcn_thread->sink_nid, pcn_thread->source_nid)))
    {
        pcn_thread->done = 1;
        return NULL;
    }
#endif

    /* Migrate Thread Back to source_nid */
    ft2c_thread_errno = migrate(pcn_thread->source_nid, NULL, NULL);

    printf("FT_2_C: We should have arrived back at source node.\n");

    /* We should be at sink_nid. Get TID at Sink Node */
    temp_tid = gettid();

    if(temp_tid == -1)
    {
        printf("FT_2_C FAILED: Thread ID is not a positive integer, TID: %d\n", temp_tid);
        ft2c_thread_errno = -1;
        pcn_thread->done = 1;
        return NULL;
    }
    else if(temp_tid != source_tid)
    {
        printf("FT_2_C FAILED: Thread ID %d does not match original TID %d\n", temp_tid, source_tid);
        ft2c_thread_errno = -1;
        pcn_thread->done = 1;
        return NULL;
    }

    printf("FT_2_C: Thread ID is %d\n", source_tid);

    /* Node Sanity Check */
    if((ft2c_thread_errno = node_sanity_check(pcn_thread->source_nid, pcn_thread->sink_nid)))
    {
        pcn_thread->done = 1;
        return NULL;
    }

    printf("FT_2_C Thread %d PASSED at NODE %d\n", source_tid, pcn_thread->source_nid);
    pcn_thread->done = 1;
    return NULL;
}

int main(int argc, char *argv[])
{

#ifdef __x86_64__
	int ft2c_errno = 0;
	int source_node;
	int sink_node;
    pid_t source_pid = gettid();

    if(source_pid == -1)
    {
        printf("FT_2_C FAILED: Process ID is not a positive integer, PID: %d\n", source_pid); // NOTE TID (TGID) should be == to PID (TID) in this instance
        ft2c_errno = -1;
        return ft2c_errno;
    }

	if (argc != 3)
    {
        printf("FT_2_C FAILED: This test takes 2 arguments, Source Node ID, Sink Node ID\n");
    }


    printf("FT_2_C: Process ID is %d\n", source_pid);

    source_node = atoi(argv[1]);
    sink_node = atoi(argv[2]);

    if(source_node == sink_node)
    {
        printf("FT_2_C FAILED: Source Node ID must be different to Sink Node ID\n");
        ft2c_errno = -1;
        return ft2c_errno;
    }
    else if((source_node | sink_node) < 0 || source_node >= MAX_POPCORN_NODES || sink_node >= MAX_POPCORN_NODES)
    {
        printf("FT_2_C FAILED: Node ID's must be a positive integer 0-31\n");
        ft2c_errno = -1;
        return ft2c_errno;
    }

    /* Init popcorn "traveling" thread */
    if((ft2c_errno = posix_memalign((void **)&(pcn_thread), PAGESZ, sizeof(struct thread))))
    {
        printf("FT_2_C FAILED: Failed to Init pcn_thread. ERROR CODE %d\n", ft2c_errno);
        return ft2c_errno;
    }
    pthread_barrier_init(&barrier_start, NULL, 2);

    pcn_thread->source_nid = source_node;
    pcn_thread->sink_nid = sink_node;
    pcn_thread->done = 0;

    /* Create and init pthread */
    pthread_create(&pcn_thread->thread_info, NULL, &child_thread, NULL);

    pthread_barrier_wait(&barrier_start);

    /* Wait for thread to finish. Spinning here is not egrigious as thread should finish quickly. */
    while(!pcn_thread->done);
    int *errno_ptr = &ft2c_errno;
    pthread_join(pcn_thread->thread_info, (void(**))(&errno_ptr));
    printf("FT_2_C TEST at NODE %d Thread %d exited with CODE %d\n", source_node, pcn_thread->tid, ft2c_errno);
    free(pcn_thread);


    printf("FT_2_C TEST PASSED at NODE %d\n", source_node);
	return ft2c_errno;
#else
    int ft2c_errno;
    printf("FT_2_C: Test only supports X86_64 Architecture\n");
    ft2c_errno = -1;
    return ft2c_errno;
#endif
}
