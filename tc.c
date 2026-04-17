#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "common.h"

static double sim_start;

void init_time() {
    sim_start = GetTime();
}

double get_time() {
    return GetTime() - sim_start;
}

typedef struct _directions {
    char dir_original;
    char dir_target;
} directions;

int dir_index(char d) {
    switch(d) { case '^': return 0; case 'v': return 1;
                case '>': return 2; default:  return 3; }
}

int get_turn(char orig, char target) {
    int ring[] = {0, 2, 1, 3}; 
    int ro = ring[dir_index(orig)];
    int rt = ring[dir_index(target)];
    int diff = (rt - ro + 4) % 4;
    if (diff == 1) return 0; // right
    if (diff == 2) return 1; // straight
    if (diff == 3) return 2; // left
    return 1;
}

double cross_time(int turn) {
    if (turn == 2) return 5.0; // left  (DeltaL = 5s)
    if (turn == 1) return 4.0; // straight (DeltaS = 4s)
    return 3.0;                // right (DeltaR = 3s)
}

pthread_mutex_t zone_mutex[4];
pthread_cond_t  zone_cond[4];
int             zone_dir[4];    
int             zone_count[4];

void zone_acquire(int z, int di) {
    pthread_mutex_lock(&zone_mutex[z]);
    
    while (zone_dir[z] != -1 && zone_dir[z] != di)
        pthread_cond_wait(&zone_cond[z], &zone_mutex[z]);
    zone_dir[z] = di;
    zone_count[z]++;
    pthread_mutex_unlock(&zone_mutex[z]);
}

void zone_release(int z) {
    pthread_mutex_lock(&zone_mutex[z]);
    zone_count[z]--;
    if (zone_count[z] == 0) {
        zone_dir[z] = -1;
        pthread_cond_broadcast(&zone_cond[z]);
    }
    pthread_mutex_unlock(&zone_mutex[z]);
}


void get_zones(char orig, int turn, int *zones, int *n) {
    *n = 0;
    switch(orig) {
        case '^': // from North
            if      (turn == 0) { zones[0]=0; *n=1; }
            else if (turn == 1) { zones[0]=0; zones[1]=1; *n=2; }
            else                { zones[0]=0; zones[1]=1; zones[2]=2; *n=3; }
            break;
        case 'v': // from South
            if      (turn == 0) { zones[0]=2; *n=1; }
            else if (turn == 1) { zones[0]=2; zones[1]=3; *n=2; }
            else                { zones[0]=2; zones[1]=3; zones[2]=0; *n=3; }
            break;
        case '>': // from East
            if      (turn == 0) { zones[0]=1; *n=1; }
            else if (turn == 1) { zones[0]=1; zones[1]=2; *n=2; }
            else                { zones[0]=1; zones[1]=2; zones[2]=3; *n=3; }
            break;
        case '<': // from West
            if      (turn == 0) { zones[0]=3; *n=1; }
            else if (turn == 1) { zones[0]=3; zones[1]=0; *n=2; }
            else                { zones[0]=3; zones[1]=0; zones[2]=1; *n=3; }
            break;
    }
}

sem_t           head_of_line[4];
pthread_mutex_t arrival_mutex = PTHREAD_MUTEX_INITIALIZER;
int global_ticket    = 0;
int stopline_ticket[4]; 


typedef struct _car {
    int        cid;
    double     arrival_time;
    directions dir;
} car_t;

void *Car(void *arg) {
    car_t *car  = (car_t *)arg;
    int    di   = dir_index(car->dir.dir_original);
    int    turn = get_turn(car->dir.dir_original, car->dir.dir_target);

    // === ARRIVE: sleep until scheduled arrival time ===
    double wait = car->arrival_time - get_time();
    if (wait > 0) usleep((useconds_t)(wait * 1e6)); // usleep for arrival time

    printf("Time %.1f: Car %d (%c %c) arriving\n",
           get_time(), car->cid,
           car->dir.dir_original, car->dir.dir_target);

    usleep(2000000);

    sem_wait(&head_of_line[di]);

    pthread_mutex_lock(&arrival_mutex);
    global_ticket++;
    int my_ticket = global_ticket;
    stopline_ticket[di] = my_ticket;
    pthread_mutex_unlock(&arrival_mutex);

    while (1) {
        pthread_mutex_lock(&arrival_mutex);
        int must_wait = 0;
        for (int i = 0; i < 4; i++) {
            if (i == di) continue;
            if (stopline_ticket[i] != 0 && stopline_ticket[i] < my_ticket) {
                must_wait = 1;
                break;
            }
        }
        pthread_mutex_unlock(&arrival_mutex);
        if (!must_wait) break;
        usleep(5000);
    }

    int zones[3], nz;
    get_zones(car->dir.dir_original, turn, zones, &nz);
    int sorted[3];
    for (int i = 0; i < nz; i++) sorted[i] = zones[i];
    for (int i = 0; i < nz-1; i++)
        for (int j = i+1; j < nz; j++)
            if (sorted[j] < sorted[i]) { int t=sorted[i]; sorted[i]=sorted[j]; sorted[j]=t; }
    if (turn != 0) {
        for (int i = 0; i < nz; i++)
            zone_acquire(sorted[i], di);
    }

    pthread_mutex_lock(&arrival_mutex);
    stopline_ticket[di] = 0;
    pthread_mutex_unlock(&arrival_mutex);
    sem_post(&head_of_line[di]);

    printf("Time %.1f: Car %d (%c %c)         crossing\n",
           get_time(), car->cid,
           car->dir.dir_original, car->dir.dir_target);

    Spin((int)cross_time(turn));

    printf("Time %.1f: Car %d (%c %c)                  exiting\n",
           get_time(), car->cid,
           car->dir.dir_original, car->dir.dir_target);

    if (turn != 0) {
        for (int i = 0; i < nz; i++)
            zone_release(sorted[i]);
    }

    return NULL;
}

int main() {
    init_time();

    for (int i = 0; i < 4; i++) {
        pthread_mutex_init(&zone_mutex[i], NULL);
        pthread_cond_init(&zone_cond[i], NULL);
        zone_dir[i]        = -1;
        zone_count[i]      = 0;
        stopline_ticket[i] = 0;
        sem_init(&head_of_line[i], 0, 1); 
    }


    car_t cars[] = {
        {1, 1.1, {'^', '^'}},
        {2, 2.2, {'^', '^'}},
        {3, 3.3, {'^', '<'}},
        {4, 4.4, {'v', 'v'}},
        {5, 5.5, {'v', '>'}},
        {6, 6.6, {'^', '^'}},
        {7, 7.7, {'>', '^'}},
        {8, 8.8, {'<', '^'}},
    };
    int n = sizeof(cars) / sizeof(cars[0]);

    pthread_t threads[n];
    for (int i = 0; i < n; i++)
        pthread_create(&threads[i], NULL, Car, &cars[i]);


    for (int i = 0; i < n; i++)
        pthread_join(threads[i], NULL);
    for (int i = 0; i < 4; i++) {
        pthread_mutex_destroy(&zone_mutex[i]);
        pthread_cond_destroy(&zone_cond[i]);
        sem_destroy(&head_of_line[i]);
    }

    return 0;
}