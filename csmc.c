// Tristan Pior
// TJP170002
// CS4348.003 Project 3

#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX_PROGRAMMING_TIME    0.002
#define MIN_PROGRAMMING_TIME    0.0002
#define TUTORING_TIME           0.0002
// #define DEBUG 1

// Structure for maintaining student priorities when added to the chairs
struct student {
    pthread_t studentThread;
    long id;
    // Lower number is a higher priority
    int priority;
    long currentTutor;
};

// List of students of size nstudents
static struct student *students;
static int nstudents;
// List of tutor threads of size ntutors
static pthread_t *tutors;
static int ntutors;
// Singular Coordinator thread
static pthread_t *coordinator;
// Chairs queue of size nchairs
static struct student **queueInput;
static struct student **chairsQueue;
static int nchairs;
static int nEmptyChairs;
static int nWaitingStudents;
// Number of times a student thread will seek help before terminating
static int nseekhelp;
// Number of students being tutored and total number tutored
static int nBeingTutored;
static int nTotalTutored;
// Mutex lock for modifying chairsQueue and queueInput
pthread_mutex_t chairsQueue_mutex;
pthread_mutex_t queueInput_mutex;
// Semaphore for waking up tutors and waking up coordinator
sem_t tutor_is_waiting;
sem_t coordinator_is_waiting;
// Termination condition for coordinator and tutor threads
static int terminate;
// Synchronization condition for all threads to wait until all threads are created
static int synchronize;

// Helper function used by the coordinator thread to add a student to the queue
// Assumes that there is room to add a student to the queue (checked in student thread via nEmptyChairs)
// Takes in a pointer to the student as an argument
// Returns 0 on success
int
addToQueue(struct student *stu) {
    int stuPriority = stu->priority;
    int insertIndex = -1;
    int i;
    pthread_mutex_lock(&chairsQueue_mutex);
    // Find index to insert at
    for(i = 0; i < nchairs; i++) {
        if(chairsQueue[i] == NULL) {
            // For when stu goes at the end of the queue
            if(insertIndex == -1) {
                insertIndex = i;
            }
            break;
        }
        if(chairsQueue[i]->priority > stuPriority) {
            insertIndex = i;
            break;
        }
    }
    if(insertIndex == -1) {
        // Insert failed
        return 1;
    }
    // Create an empty space at insertIndex
    for(i = nchairs - 1; i > insertIndex; i--) {
        chairsQueue[i] = chairsQueue[i - 1];
    }
    // Insert the student to the queue
    chairsQueue[insertIndex] = stu;
    nWaitingStudents++;
    pthread_mutex_unlock(&chairsQueue_mutex);
    return 0;
}

// Helper function used by the tutor threads to remove a student from the queue
// Assumes calling function already has a lock on chairsQueue_mutex
// Removes the index 0 student from the queue and moves the rest down one index (i.e. 1 -> 0, 2 -> 1, etc)
// Returns 0 on success
int
popFromQueue() {
    // pthread_mutex_lock(&chairsQueue_mutex);
    if(chairsQueue[0] == NULL) {
        // pthread_mutex_unlock(&chairsQueue_mutex);
        return 0;
    }
    int i;
    for(i = 0; i < nchairs - 1; i++) {
        // Early exit condition: 
        // If pointer in the queue is NULL then all following pointers are NULL
        if(chairsQueue[i] == NULL) {
            break;
        }
        chairsQueue[i] = chairsQueue[i + 1];
    }
    chairsQueue[i] = NULL;
    nEmptyChairs++;
    // pthread_mutex_unlock(&chairsQueue_mutex);
    return 0;
}

// Helper function used by the coordinator thread to remove a student from the input queue
// Removes the index 0 student from the queueInput
// Moves all other students in queueInput down one index (i.e. 1 -> 0, 2 -> 1, etc)
// Returns 0 on success
int
popFromInputQueue() {
    pthread_mutex_lock(&queueInput_mutex);
    if(queueInput[0] == NULL) {
        pthread_mutex_unlock(&queueInput_mutex);
        return 0;
    }
    int i;
    for(i = 0; i < nchairs - 1; i++) {
        // Early exit condition: 
        // If pointer in the queue is NULL then all following pointers are NULL
        if(queueInput[i] == NULL) {
            break;
        }
        queueInput[i] = queueInput[i + 1];
    }
    queueInput[i] = NULL;
    pthread_mutex_unlock(&queueInput_mutex);
    return 0;
}

/*
Wait for a student to notify of entering the queue (coordinator_is_waiting)
Add student to the chairsQueue based on priority (removed from queue by tutor)
Removes the index 0 student from the queueInput (popFromInputQueue)
Output Message
Notify a tutor of a waiting student (tutor_is_waiting)
*/
static void *
coordinatorThread(void *xa) {
    #ifdef DEBUG
        if(DEBUG) printf("Coordinator thread created\n");
    #endif
    int totalRequests = 0;
    while(synchronize) { /* Do nothing */ }
    while(terminate) {
        sem_wait(&coordinator_is_waiting);
        // Check terminate condition
        if(!terminate) {
            int i;
            for(i = 0; i < ntutors; i++) {
                sem_post(&tutor_is_waiting);
            }
            return NULL;
        }
        totalRequests++;
        // pthread_mutex_lock(&queueInput_mutex);
        if(queueInput[0] == NULL) {
            pthread_mutex_unlock(&queueInput_mutex);
            continue;
        }
        // pthread_mutex_unlock(&queueInput_mutex);
        long stuID = queueInput[0]->id;
        int stuPriority = queueInput[0]->priority;
        if(addToQueue(queueInput[0])) {
            // Insert failed
            #ifdef DEBUG
                if(DEBUG) printf("Coordinator addToQueue failed\n");
            #endif
        }
        printf("C: Student %ld with priority %d added to the queue. Waiting students now = %d. Total requests = %d.\n", stuID, stuPriority, nWaitingStudents, totalRequests);
        if(popFromInputQueue()) {
            // Insert failed
            #ifdef DEBUG
                if(DEBUG) printf("Coordinator popFromInputQueue failed\n");
            #endif
        }
        sem_post(&tutor_is_waiting);
    }
    return NULL;
}

/*
Waits to be notified of a waiting student (tutor_is_waiting)
Finds the student with the highest priority (index 0 of chairsQueue)
Removes that student from the queue and moves the rest down one index (i.e. 1 -> 0, 2 -> 1, etc)
Wakes up that student
Sleep for TUTORING_TIME
Output Message
*/
static void *
tutorThread(void *xa) {
    long id = (long)xa;
    #ifdef DEBUG
        if(DEBUG) printf("Tutor thread %ld created\n", id);
    #endif
    while(synchronize) { /* Do nothing */ }
    while(terminate) {
        sem_wait(&tutor_is_waiting);
        // Check terminate condition
        if(!terminate) {
            return NULL;
        }
        pthread_mutex_lock(&chairsQueue_mutex);
        if(chairsQueue[0] == NULL) {
            pthread_mutex_lock(&chairsQueue_mutex);
            continue;
        }
        struct student *stu = chairsQueue[0];
        if(popFromQueue()) {
            #ifdef DEBUG
                if(DEBUG) printf("Tutor %ld popFromQueue failed\n", id);
            #endif
        }
        pthread_mutex_unlock(&chairsQueue_mutex);
        nWaitingStudents--;
        stu->currentTutor = id;
        nBeingTutored++;
        sleep(TUTORING_TIME);
        nBeingTutored--;
        nTotalTutored++;
        printf("T: Student %ld tutored by Tutor %ld. Students tutored now = %d. Total sessions tutored = %d.\n", stu->id, id, nBeingTutored, nTotalTutored);
    }
    return NULL;
}

/* 
Starts by programming (sleep for a random amount of time)
Looks for an empty chair in queueInput (nEmptyChairs > 0)
If there is not an empty chair, then go back to programming
If there is an empty chair:
Wait to be woken up by a tutor
Sleep for TUTORING_TIME
Output Message
*/
static void *
studentThread(void *xa) {
    long id = (long) xa;
    #ifdef DEBUG
        if(DEBUG) printf("Student thread %ld created\n", id);
    #endif
    int timeshelped = 0;
    while(synchronize) { /* Do nothing */ }
    while(timeshelped < nseekhelp) {
        float programmingTime = MIN_PROGRAMMING_TIME + ((float)rand() / (float)RAND_MAX) * (MAX_PROGRAMMING_TIME - MIN_PROGRAMMING_TIME);
        sleep(programmingTime);
        int i;
        pthread_mutex_lock(&queueInput_mutex);
        if(nEmptyChairs == 0) {
            // printf("S: Student %ld found no empty chair. Will try again later.\n", id);
            pthread_mutex_unlock(&queueInput_mutex);
            continue;
        }
        // Find an empty chair
        for(i = 0; i < nchairs; i++) {
            if(queueInput[i] == NULL) {
                break;
            }
        }
        queueInput[i] = &students[id];
        nEmptyChairs--;
        printf("S: Student %ld takes a seat. Empty chairs = %d.\n", id, nEmptyChairs);
        pthread_mutex_unlock(&queueInput_mutex);
        sem_post(&coordinator_is_waiting);
        // Wait for a tutor
        while(students[id].currentTutor == -1) { /* Do nothing */ }
        sleep(TUTORING_TIME);
        printf("S: Student %ld received help from Tutor %ld.\n", id, students[id].currentTutor);
        students[id].currentTutor = -1;
        timeshelped++;
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    // Check for the correct number of arguments
    if (argc < 5) {
        fprintf(stderr, "%s: invalid number of arguments\n", argv[0]);
        exit(1);
    }
    // Initialize values
    nstudents = atoi(argv[1]);
    ntutors = atoi(argv[2]);
    nchairs = atoi(argv[3]);
    nEmptyChairs = nchairs;
    nWaitingStudents = 0;
    nseekhelp = atoi(argv[4]);
    nBeingTutored = 0;
    nTotalTutored = 0;
    terminate = 1;
    synchronize = 1;
    srand(0);
    // Initialize semaphores
    sem_init(&tutor_is_waiting, 0, 0);
    sem_init(&coordinator_is_waiting, 0, 0);
    // Initialize mutexes
    pthread_mutex_init(&chairsQueue_mutex, NULL);
    pthread_mutex_init(&queueInput_mutex, NULL);

    // Malloc threads
    students = malloc(sizeof(struct student) * nstudents);
    tutors = malloc(sizeof(pthread_t) * ntutors);
    coordinator = malloc(sizeof(pthread_t));
    // Malloc chairs queue
    chairsQueue = malloc(sizeof(struct student *) * nchairs);
    queueInput = malloc(sizeof(struct student *) * nchairs);
    // Set chairs queue empty
    long i;
    for(i = 0; i < nchairs; i++) {
        chairsQueue[i] = NULL;
        queueInput[i] = NULL;
    }

    // Create coordinator thread
    assert(pthread_create(coordinator, NULL, coordinatorThread, (void *)0) == 0);
    // Create student threads
    for (i = 0; i < nstudents; i++) {
        students[i].priority = 0;
        students[i].currentTutor = -1;
        students[i].id = i;
        assert(pthread_create(&students[i].studentThread, NULL, studentThread, (void *)i) == 0);
    }
    // Create tutor threads
    for (i = 0; i < ntutors; i++) {
        assert(pthread_create(&tutors[i], NULL, tutorThread, (void *)i) == 0);
    }
    // Tell all threads to begin running
    synchronize = 0;

    //Wait for all student threads to terminate
    for(i = 0; i < nstudents; i++) {
        pthread_join(students[i].studentThread, NULL);
        #ifdef DEBUG
            if(DEBUG) printf("Student %ld terminated\n", i);
        #endif
    }
    // Tell coordinator and tutors to terminate and synchronize
    terminate = 0;
    sem_post(&coordinator_is_waiting);
    for(i = 0; i < ntutors; i++) {
        pthread_join(tutors[i], NULL);
        #ifdef DEBUG
            if(DEBUG) printf("Tutor %ld terminated\n", i);
        #endif
    }
    pthread_join(*coordinator, NULL);
    #ifdef DEBUG
        if(DEBUG) printf("Coordinator terminated");
    #endif
    return 0;
}