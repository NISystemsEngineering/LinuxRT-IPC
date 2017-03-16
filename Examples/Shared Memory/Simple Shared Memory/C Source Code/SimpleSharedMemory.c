/*
 * SimpleSharedMemory.c
 *
 *  Created on: Jan 17, 2017
 *      Author: Administrator
 */

#define _GNU_SOURCE

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sched.h>

#define SHM "/simple"
#define PATH "/dev/shm/simple"
#define BILLION 1000000000

/* Print Colors */
#define TRED "\x1B[31m"
#define TNORM "\x1B[0m"
#define TGREEN "\x1B[32m"
#define TCYAN "\x1B[36m"

// addTimes adds second timespec to first timespec
void addTimes(struct timespec *time, struct timespec add){
	time->tv_sec += add.tv_sec;
	time->tv_nsec += add.tv_nsec;

	if(time->tv_nsec > BILLION){
		time->tv_nsec -= BILLION;
		time->tv_sec += 1;
	}
}

// Calculates the difference, in seconds, between two timespecs.
double timeDiff(struct timespec *startTp, struct timespec *finishTp){
	double start = startTp->tv_sec + ((double) startTp->tv_nsec) / BILLION;
	double finish = finishTp->tv_sec + ((double) finishTp->tv_nsec) / BILLION;
	return (finish - start);
}

void printWithStatus(const char *prefix, int occurred){
	printf("%s", prefix);

	if(occurred)
		printf("%sFailed.%s\n", TRED, TNORM);
	else
		printf("%sSuccess.%s\n", TGREEN, TNORM);
}

int main(int argc, char *argv[]){
	// Variables for loops, logic, etc.
	double remainder;
	int ret;

	// Structures for timing
	struct timespec tp, prev, now, periodTp, timeout, timeoutDuration;
	periodTp.tv_sec = 0;
	periodTp.tv_nsec = 10000000; // 10,000,000 nanoseconds = 10 ms
	timeoutDuration.tv_sec = 10; // Use timeout of 10 seconds for waiting on LabVIEW.

	// Clear terminal.
	printf("\033c");

	// Lock the process to core 1.
	cpu_set_t cpu;
	CPU_ZERO(&cpu);
	CPU_SET(1, &cpu);
	printf("Setting CPU Affinity to CPU%d... ", 1);
	ret = sched_setaffinity(0, sizeof(cpu_set_t), &cpu);
	printWithStatus("", ret);

	// Elevate this process to highest priority.
	struct sched_param params;
	params.sched_priority = 99;
	printf("Setting thread priority to %d... ", 99);
	ret = pthread_setschedparam(pthread_self(), SCHED_FIFO, &params);
	printWithStatus("", ret);

	// Wait for LabVIEW to create shared memory by checking if it exists.
	printf("Waiting for LabVIEW to create shared memory...\n");
	clock_gettime(CLOCK_MONOTONIC, &timeout);
	addTimes(&timeout, timeoutDuration);

	do{
		// Get current time.
		clock_gettime(CLOCK_MONOTONIC, &tp);

		// Get remainder until timeout occurs.
		remainder = timeDiff(&tp, &timeout);
		printf("\rTimeout: %1.2f  \n", remainder); // Adding new line tells terminal to print immediately.
		printf("\033[1A"); // Move backup one line to overwrite previous line.

		if(remainder < 0)
			break;

		addTimes(&tp, periodTp);
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &tp, NULL);
	} while(access(PATH, F_OK) != 0);

	// Exit application if timeout was reached.
	if(remainder < 0){
		printf("\rLabVIEW did not create shared memory within timeout period. Exiting.\n\n");
		return 0;
	}
	else;
		//printf("\rShared memory file detected. Opening file descriptor to %s... ", SHM);

	// Open file descriptor to shared memory.
	int fd = shm_open(SHM, O_RDWR, (mode_t) 0777);
	printWithStatus("\rShared memory file detected.\nOpening file descriptor... ", (fd < 0));

	// Map memory from shared memory to application memory.
	void *mmapPointer = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	printWithStatus("Mapping memory... ", (mmapPointer == (void *) -1));

	// Since file descriptor is no longer needed after mmap, close it.
	ret = close(fd);
	printWithStatus("Closing file descriptor... ", (ret != 0));

	// Create new pointers from base pointer returned by mmap(). These will be used to read data from LabVIEW.
	// Note: The order the pointers are derived must match LabVIEW's order to prevent garbage data.
	void *pTemp = mmapPointer;

	uint8_t  *lv_stop = (uint8_t*) pTemp;
	pTemp += sizeof(uint8_t);

	uint8_t  *lv_bool = (uint8_t*) pTemp;
	pTemp += sizeof(uint8_t);

	double   *lv_double = (double*) pTemp;
	pTemp += sizeof(double);

	int32_t *lv_int = (int32_t*) pTemp;
	pTemp += sizeof(int32_t);

	// Enter main while loop.
	printf("\nRunning main loop until LabVIEW asserts STOP... \n");
	clock_gettime(CLOCK_MONOTONIC, &now);

	do{
		// Get current time.
		prev = now;
		clock_gettime(CLOCK_MONOTONIC, &now);

		// Print loop period.
		printf("\rPeriod: %02.3f ms   ", (timeDiff(&prev, &now) * 1000));

		// Read and print LabVIEW values. As simple as reading contents of pointers.
		printf("\nBoolean:   %1d  ", *lv_bool);
		printf("\nDouble:    %+f             ", *lv_double);
		printf("\nI32:       %+d             ", *lv_int);

		// Move cursor up 4 lines to overwrite previous values.
		printf("\n\033[4A");

		// Sleep for period.
		tp = now;
		addTimes(&tp, periodTp);
		do{
			ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &tp, NULL);
		} while(ret == EINTR && ret != 0);
	} while(!*lv_stop);

	printf("\n\n\n\nLabVIEW asserted STOP.\n");
	printf("Exiting.\n\n");

	return 0;
}
