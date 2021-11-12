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
#define DEBUG 1

// Structure for maintaining student priorities when added to the chairs
struct student {
    pthread_t studentThread;
    long id;
    // Lower number is a higher priority
    int priority;
    long currentTutor;
    int waiting;
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
static struct student **chairs;
static int nchairs;
static int nEmptyChairs;
// Number of times a student thread will seek help before terminating
static int nseekhelp;

// Mutex lock for modifying chairs queue
pthread_mutex_t chairs_mutex;
// Semaphore for waking up tutors and waking up coordinator
sem_t tutor_is_waiting;
sem_t coordinator_is_waiting;
// Termination condition for coordinator and tutor threads
int terminate;
// Waiting condition for all threads to be created
int allCreated;

// Total number of students tutored and current number being tutored
int nTotalTutored;
int nBeingTutored;

static void *
coordinatorThread(void *xa) {
    #ifdef DEBUG
        if(DEBUG) printf("Coordinator thread created\n");
    #endif
    while(allCreated) {
        // Do nothing
    }
    int nTotalRequests = 0;
    while(terminate) {
        // Wait to be notified of a waiting student
        sem_wait(&coordinator_is_waiting);
        nTotalRequests++;
        //Find student added to queue
        pthread_mutex_lock(&chairs_mutex);
        int i;
        int sID = -1;
        for(i = 0; i < nchairs; i++) {
            if(chairs[i] == NULL) {
                continue;
            }
            if(chairs[i]->waiting) {
                chairs[i]->waiting = 0;
                sID = chairs[i]->id;
                break;
            }
        }
        // Check if a student was actually found
        if(sID == -1) {
            pthread_mutex_unlock(&chairs_mutex);
            continue;
        }
        printf("C: Student %d with priority %d added to the queue. Waiting students now = %d. Total requests = %d.\n", sID, students[sID].priority, nchairs - nEmptyChairs, nTotalRequests);
        pthread_mutex_unlock(&chairs_mutex);
        // Check terminate condition
        if(!terminate) {
            // Free all tutors
            int i;
            for(i = 0; i < ntutors; i++) {
                sem_post(&tutor_is_waiting);
            }
            return NULL;
        }
        // Notify free tutor of waiting student
        sem_post(&tutor_is_waiting);
    }
    return NULL;
}

static void *
studentThread(void *xa) {
    #ifdef DEBUG
        if(DEBUG) printf("Student thread %ld created\n", (long)xa);
    #endif
    long id = (long) xa;
    students[id].id = id;
    // Wait for all threads to be created
    while(allCreated) {
        // Do nothing
    }
    while(students[id].priority < nseekhelp) {
        // Start by "programming" for a random amount of time between [MIN_PROGRAMMING_TIME, MAX_PROGRAMMING_TIME]
        // rand() / RAND_MAX = [0.0, 1.0] and Multiply by our range to get from [0.0, MAX_PROGRAMMING_TIME - MIN_PROGRAMMING_TIME]
        // Finally add the minimum time MIN_PROGRAMMING_TIME to get a range of [MIN_PROGRAMMING_TIME, MAX_PROGRAMMING_TIME]
        float programmingTime = MIN_PROGRAMMING_TIME + ((float)rand() / (float)RAND_MAX) * (MAX_PROGRAMMING_TIME - MIN_PROGRAMMING_TIME);
        sleep(programmingTime);

        // Check for empty chair:
        pthread_mutex_lock(&chairs_mutex);
        int i;
        if(nEmptyChairs == 0) {
            printf("S: Student %ld found no empty chair. Will try again later.\n", id);
            pthread_mutex_unlock(&chairs_mutex);
            continue;
        }
        for(i = 0; i < nchairs; i++) {
            if(chairs[i] == NULL) {
                break;
            }
        }
        // If there is no empty chair then go back to programming
        // if(i >= nchairs) {
        //     printf("S: Student %ld found no empty chair. Will try again later.\n", id);
        //     pthread_mutex_unlock(&chairs_mutex);
        //     continue;
        // }
        // If there is an empty chair then take it and notify the coordinator with a semaphore to be queued
        students[id].waiting = 1;
        chairs[i] = &students[id];
        nEmptyChairs--;
        pthread_mutex_unlock(&chairs_mutex);
        printf("S: Student %ld takes a seat. Empty chairs = %d.\n", id, nEmptyChairs);
        sem_post(&coordinator_is_waiting);
        // Get woken up by tutor, sleep for 0.2ms for "tutoring", and empty the chair
        while(students[i].currentTutor == -1) {
            // Do nothing
        }
        sleep(TUTORING_TIME);
        printf("S: Student %ld received help from Tutor %ld.\n", id, students[id].currentTutor);
        students[id].currentTutor = -1;
        // students[id].waiting = 0;
        pthread_mutex_lock(&chairs_mutex);
        chairs[i] = NULL;
        nEmptyChairs++;
        pthread_mutex_unlock(&chairs_mutex);
        // Decrease priority
        students[id].priority++;
    }
    return NULL;
}

static void *
tutorThread(void *xa) {
    long id = (long)xa;
    #ifdef DEBUG
        if(DEBUG) printf("Tutor thread %ld created\n", id);
    #endif
    while(allCreated) {
        // Do nothing
    }
    while(terminate) {
        // Starts by waiting for coordinator
        sem_wait(&tutor_is_waiting);
        // Check terminate condition
        if(!terminate) {
            return NULL;
        }
        // Gets woken up by coordinator
        // Wake up the highest priority student in the queue and sleep for 0.2ms for "tutoring"
        int i;
        int highestPriority;
        int highestPriorityIndex = -1;
        // Set starting value for comparison
        pthread_mutex_lock(&chairs_mutex);
        for(i = 0; i < nchairs; i++) {
            if(chairs[i] == NULL || chairs[i]->currentTutor > -1) {
                continue;
            }
            highestPriority = chairs[i]->priority;
            highestPriorityIndex = i;
            break;
        }
        //Check if highestPriority variables are set
        if(highestPriorityIndex < 0) {
            pthread_mutex_unlock(&chairs_mutex);
            continue;
        }
        // Compare against all other chairs to find the highest priority student
        for(i = 0; i < nchairs; i++) {
            if(chairs[i] == NULL || chairs[i]->currentTutor > -1) {
                continue;
            }
            if(chairs[i]->priority < highestPriority) {
                highestPriority = chairs[i]->priority;
                highestPriorityIndex = i;
            }
        }
        chairs[highestPriorityIndex]->currentTutor = id;
        long sID = chairs[highestPriorityIndex]->id;
        pthread_mutex_unlock(&chairs_mutex);
        nBeingTutored++;
        sleep(TUTORING_TIME);
        nBeingTutored--;
        nTotalTutored++;
        printf("T: Student %ld tutored by Tutor %ld. Students tutored now = %d. Total sessions tutored = %d.\n", sID, id, nBeingTutored, nTotalTutored);
        // Go back to waiting for coordinator
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    // Check for the correct number of arguments
    if (argc < 5) {
        fprintf(stderr, "%s: invalid number of arguments\n", argv[0]);
        exit(1);
    }

    nstudents = atoi(argv[1]);
    ntutors = atoi(argv[2]);
    nchairs = atoi(argv[3]);
    nEmptyChairs = nchairs;
    nseekhelp = atoi(argv[4]);

    sem_init(&tutor_is_waiting, 0, 0);
    sem_init(&coordinator_is_waiting, 0, 0);

    terminate = 1;
    allCreated = 1;
    nBeingTutored = 0;
    nTotalTutored = 0;

    srand(0);

    // Malloc students, tutors, and coordinator thread lists
    students = malloc(sizeof(struct student) * nstudents);
    tutors = malloc(sizeof(pthread_t) * ntutors);
    coordinator = malloc(sizeof(pthread_t));
    // Malloc chairs queue
    chairs = malloc(sizeof(struct student *) * nchairs);
    // Set chairs queue empty
    long i;
    for(i = 0; i < nchairs; i++) {
        chairs[i] = NULL;
    }

    // Create coordinator thread
    assert(pthread_create(coordinator, NULL, coordinatorThread, (void *)0) == 0);
    // Create student threads
    for (i = 0; i < nstudents; i++) {
        students[i].priority = 0;
        students[i].currentTutor = -1;
        students[i].waiting = 0;
        assert(pthread_create(&students[i].studentThread, NULL, studentThread, (void *)i) == 0);
    }
    // Create tutor threads
    for (i = 0; i < ntutors; i++) {
        assert(pthread_create(&tutors[i], NULL, tutorThread, (void *)i) == 0);
    }
    // Tell all threads that all threads have been made and they can begin running
    allCreated = 0;
    //Wait for all student threads to terminate
    for(i = 0; i < nstudents; i++) {
        pthread_join(students[i].studentThread, NULL);
        #ifdef DEBUG
            if(DEBUG) printf("Student %ld terminated\n", i);
        #endif
    }
    // Tell coordinator and tutors to terminate and synchronize
    terminate = 0;
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