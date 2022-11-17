#define _GNU_SOURCE
#include <errno.h> // for EINTR
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "common.h"
#include "common_threads.h"

// Print out the usage of the program and exit.
void Usage(char *);
// Hashing function
uint32_t jenkins_one_at_a_time_hash(const uint8_t *, uint64_t);
// Thread function
void *tree(void *arg);

// block size
#define BSIZE 4096
#define MAX_HASH_LENGTH 30

// global variables
size_t fileSize; // Size of the input file
size_t BPT;      // Bytes Per Thread
int32_t fd;
int numThreads; // number of threads specified by user
void *memmap;

int main(int argc, char **argv) {
  uint32_t nblocks;
  uint32_t threadID = 0;
  struct stat sb;

  numThreads = atoi(argv[2]);

  // input checking
  if (argc != 3)
    Usage(argv[0]);

  // open input file
  fd = open(argv[1], O_RDWR);
  if (fd == -1) {
    perror("open failed");
    exit(EXIT_FAILURE);
  }

  // use fstat to get file size
  if (fstat(fd, &sb) == -1) {
    printf("fstat error");
    exit(EXIT_SUCCESS);
  }
  // calculate nblocks
  fileSize = (size_t)sb.st_size;
  nblocks = fileSize / BSIZE;

  memmap = mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);

  // calculating the number of blocks each thread has to hash
  BPT = (size_t)((nblocks / numThreads) * BSIZE);

  printf(" no. of blocks = %u \n", nblocks);

  double start = GetTime();

  // calculate hash value of the input file
  // creating the root node and initializing hash
  pthread_t root;
  void *hash = NULL;
  numThreads--;

  pthread_create(&root, NULL, tree, &threadID);
  pthread_join(root, &hash);

  double end = GetTime();

  printf("hash value = %s \n", (char *)hash);
  printf("time taken = %f \n", (end - start));
  close(fd);
  return EXIT_SUCCESS;
}

uint32_t jenkins_one_at_a_time_hash(const uint8_t *key, uint64_t length) {
  uint64_t i = 0;
  uint32_t hash = 0;

  while (i != length) {
    hash += key[i++];
    hash += hash << 10;
    hash ^= hash >> 6;
  }
  hash += hash << 3;
  hash ^= hash >> 11;
  hash += hash << 15;
  return hash;
}

void Usage(char *s) {
  fprintf(stderr, "Usage: %s filename num_threads \n", s);
  exit(EXIT_FAILURE);
}

void *tree(void *arg) {
  size_t offset;
  size_t pa_offset;
  void *startAddr;
  uint32_t leftChildID;
  uint32_t rightChildID;

  if ((startAddr = (void *)malloc(BPT * sizeof(void *))) == NULL) {
    printf("Malloc failed, exiting program");
    exit(EXIT_SUCCESS);
  }

  uint32_t *parentID = (uint32_t *)arg;

  // adjusting variables to use with hash function
  offset = (*parentID) * BPT;
  pa_offset = offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
  startAddr = mmap(NULL, BPT, PROT_READ, MAP_PRIVATE, fd, pa_offset);

  uint32_t hash = jenkins_one_at_a_time_hash((const uint8_t *)startAddr, BPT);

  // finding length of the hash value and then storing it into string
  char *string;
  if ((string = (char *)malloc(MAX_HASH_LENGTH * sizeof(char *))) == NULL) {
    printf("Malloc Failed, exiting program");
    exit(EXIT_SUCCESS);
  }
  sprintf(string, "%d", hash);
  string[strlen(string)] = 0;

  void *leftHash = NULL;
  void *rightHash = NULL;

  // number of threads is not a power of 2 so have to handle node with single
  // child
  if (numThreads == 1) {
    numThreads--;

    // giving the child a thread id
    leftChildID = 2 * (leftChildID) + 1;

    pthread_t pLeft;

    // create and join the thread
    pthread_create(&pLeft, NULL, tree, &leftChildID);
    pthread_join(pLeft, leftHash);

    // concatenating the new hash
    strcat(string, (char *)leftHash);

    // rehashing the string
    hash = jenkins_one_at_a_time_hash((const uint8_t *)string, strlen(string) - 1);
    free(string);
    if ((string = (char *)malloc(MAX_HASH_LENGTH * sizeof(char *))) == NULL) {
      printf("Malloc Failed, exiting program");
      exit(EXIT_SUCCESS);
    }
    sprintf(string, "%d", hash);
  }

  if (numThreads > 1) {
    numThreads = numThreads - 2;

    // adjusting the threadID of the child threads
    leftChildID = 2 * (leftChildID) + 1;
    rightChildID = 2 * (rightChildID) + 2;

    pthread_t pLeft, pRight;

    // Creating both child threads
    pthread_create(&pLeft, NULL, tree, &leftChildID);
    pthread_create(&pRight, NULL, tree, &rightChildID);

    // waiting for the child threads to end and return
    pthread_join(pLeft, &leftHash);
    pthread_join(pRight, &rightHash);

    // concatenating the new hashes
    strcat(string, (char *)leftHash);
    strcat(string, (char *)rightHash);

    // rehashing the string
    hash = jenkins_one_at_a_time_hash((const uint8_t *)string, strlen(string) - 1);
    free(string);
    if ((string = (char *)malloc(MAX_HASH_LENGTH * sizeof(char *))) == NULL) {
      printf("Malloc Failed, exiting program");
      exit(EXIT_SUCCESS);
    }
    sprintf(string, "%d", hash);
  }

  munmap(startAddr, BPT);

  pthread_exit(string);
}