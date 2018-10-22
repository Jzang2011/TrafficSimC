#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/mman.h>
#include <asm/unistd.h> //this should use the one in linux-2.6.23.1

/**
 * 1. Treat the road as two queues, and have a producer for each direction putting cars into the
 *    queues at the appropriate times.
 * 2. Have a consumer (flagperson) that allows cars from one direction to pass through the work area
 *    as described above.
 * 3. To get an 80% chance of something, you can generate a random number modulo 10, and see if
 *    its value is less than 8. It’s like flipping an unfair coin.
 * 4. Use the syscall nanosleep() or sleep() to pause your processes
 * 5.
 */

#define CAR_QUEUE_SIZE 10
#define NUM_SEMS 5

typedef enum {
    NORTH,
    SOUTH
} Direction;

/**
 * Car struct.
 */
typedef struct Car {
    int car_id;
    struct tm* timeinfo; //Time of the car.
    Direction dir; //The direction the car is traveling.
} Car;

/**
 * car_queue uses circular queue implementation.
 */
struct car_queue {
//    void *buffer[10];
    struct Car buffer[10];
    int head;
    int tail;
    int count;
    int size;
};

/**
 * cs1550_sem
 */
struct cs1550_sem {
    int value;
    struct node* head; //Process queue.
    struct node* tail; //Tail node for the process queue.
};

/**
 * Checks to see if the process queue is full.
 * @param queue - the queue to be checked.
 * @return - 1 if the queue is full and 0 otherwise.
 */
int is_full(struct car_queue *queue) {
    if ((queue->head == queue->tail) && (queue->count == queue->size)) {
        return 1;
    }
    return 0;
}

/**
 * Checks to see if the car_queue is empty.
 * @param queue - the queue to be checked.
 * @return - 1 for empty and 0 otherwise.
 */
int is_empty(struct car_queue *queue) {
    if ((queue->head == queue->tail) && (queue->count == 0)) {
        return 1;
    }
    return 0;
}

/**
 * Enqueues a car in a car_queue.
 * @param queue - pointer to the queue to add to.
 * @param item - pointer to the car being added.
 */
void enqueue(struct car_queue *queue, struct Car* item) {
    queue->buffer[queue->tail % CAR_QUEUE_SIZE] = *item;
    queue->tail = (queue->tail + 1) % CAR_QUEUE_SIZE;
    queue->count++;
}

/**
 * Dequeues from a car queue.
 * @param queue - queue to be dequeued from
 * @return a void* to the item or 0 if the queue is empty.
 */
struct Car* dequeue(struct car_queue *queue) {
    if (!is_empty(queue)) {
        struct Car* item = &(queue->buffer[queue->head % CAR_QUEUE_SIZE]);
        queue->head = (queue->head + 1) % CAR_QUEUE_SIZE;
        queue->count--;
        return item;
    }
    return 0;
}

/**
 * Initializes a queue's fields.
 * @param queue to be initialized. 
 */
void init_queue(struct car_queue * queue) {
    queue->tail = 0;
    queue->head = 0;
    queue->count = 0;
    queue->size = CAR_QUEUE_SIZE;
}


typedef struct {
    struct cs1550_sem* nb_full;
    struct cs1550_sem* nb_empty;

    struct cs1550_sem* sb_full;
    struct cs1550_sem* sb_empty;

    struct cs1550_sem* sem_mutex;
} my_sems;

my_sems sems;

int* car_id_count;
Direction* current_direction;

struct car_queue* north_bound;
struct car_queue* south_bound;

/**
* Used to call our modified syscall in the linux kernel to down our semaphore.
* @param sem - the semaphore being down'ed
*/
void down(struct cs1550_sem *sem) {
    syscall(__NR_cs1550_down, sem);
}

/**
* Used to call our modified syscall in the linux kernel to up our semaphore.
* @param sem - the semaphore being up'ed
*/
void up(struct cs1550_sem *sem) {
    syscall(__NR_cs1550_up, sem);
}

/**
* Calculates the size of the memory needed to be mapped.
* @return N - the number of bytes.
*/
int calculate_mem_size() {
    printf("Calculating Mem size \n");
    int N = 0;
    N = N + sizeof(struct car_queue); //Size of one queue
    N = N + sizeof(struct car_queue); //Size of another queue
    N = N + sizeof(int);
    N = N + sizeof(Direction);
    N = N + (sizeof(struct cs1550_sem) * NUM_SEMS); //we have 5 semaphores
//    printf("size of mapped mem is %d\n", N);
    return N;
}

void init_ptrs(void* ptr_to_mem) {
//    printf("initializing pointers \n");
//    int sizeOfSem = sizeof(struct cs1550_sem);
//    printf("size of a cs1550_sem is %d\n", sizeOfSem);
//    printf("size of a car_queue is %d\n", (int) sizeof(struct car_queue));
//    printf("size of a direciton is %d\n", (int) sizeof(Direction));

//    printf("MMAPed addr is %p\n", ptr_to_mem);
//    printf("addr of last mmaped byte is %p\n", ptr_to_mem + 340);
    sems.sem_mutex = ptr_to_mem;
//    printf("addr of a sem_mutex is %p\n", sems.sem_mutex);
    sems.nb_full = sems.sem_mutex + 1;
//    printf("addr of a nb_full is %p\n", sems.nb_full);
    sems.sb_full = sems.nb_full + 1;
//    printf("addr of a sb_full is %p\n", sems.sb_full);
    sems.nb_empty = sems.sb_full + 1;
//    printf("addr of a nb_empty is %p\n", sems.nb_empty);
    sems.sb_empty = sems.nb_empty + 1;
//    printf("addr of a sb_empty is %p\n", sems.sb_empty);
    car_id_count = (int*) (sems.sb_empty + 1);
//    printf("addr of a car_id_count is %p\n", car_id_count);
    current_direction = (Direction*) (car_id_count + 1);
//    printf("addr of a current_direction is is %p\n", current_direction);
    north_bound = (struct car_queue*)(current_direction + 1);
//    printf("addr of a north_bound is %p\n", north_bound);
    south_bound = north_bound + 1;
//    printf("addr of a south_bound is %p\n", south_bound);
}

/**
* Used to initialize the simulaiton
*  - Initializes 2 producers (Northbound  and Southbound)
*  - Initializes 1 consumer (flagperson)
*/
void init_sim() {
    printf("initializing simulation.\n");
    //Initialize memory space
    int N = calculate_mem_size();
    void* ptr = mmap(NULL, N, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
    init_ptrs(ptr); //initializes the global pointers.
    //Initialize values in sems.
    sems.sb_full->value = 0;
    sems.nb_full->value = 0;
    sems.sb_empty->value = CAR_QUEUE_SIZE;
    sems.nb_empty->value = CAR_QUEUE_SIZE;
    sems.sem_mutex->value = 1;
    //Setting car queue sizes to QUEUE_SIZE.
    init_queue(north_bound);
    init_queue(south_bound);

    //initialize the task queue pointers.
    sems.sb_empty->head = NULL;
    sems.sb_empty->tail = NULL;
    sems.sb_full->head = NULL;
    sems.sb_full->tail = NULL;
    sems.nb_empty->head = NULL;
    sems.nb_empty->tail = NULL;
    sems.nb_full->head = NULL;
    sems.nb_full->tail = NULL;
    sems.sem_mutex->head = NULL;
    sems.sem_mutex->tail = NULL;

}

/**
* Generates the chance there is a car following another.
* @return Returns 1 for positive (true) chancel
* Returns 0 for negative (false).
*/
int chance_80(){
    int r = rand() % 10;
    if (r < 8) {
        return 1;
    } else {
        return 0;
    }
}

void delay_20_sec() {
    //once no car comes, there is a 20 second delay before any new car will come.
    //sleep for 20 seconds to simulate this.
    printf("sleeping for 20\n");
    sleep(20);
}

void let_car_through() {
    //Remove car from either Northbound or Southbound queues.
    //Each car takes 2 seconds to go through the construction area.
    //Sleep for 2 seconds to simulate this.
    sleep(2);
}

/**
* Gets the current system time.
*  use timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec to get hours, minutes and seconds.
* @return A struct tm* which represents the current local system time.
*/
struct tm* get_time() {
    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
//    printf ("Current local time and date: %d:%d:%d",timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    return timeinfo;
}

//Is this the best way to get the time from a car?
/**
 * Gets the local time based on the timeinfo of the car.
 * @param c - The car to get the time of.
 */
void get_car_time(Car *c) {
    time_t rawtime;
    time(&rawtime);
    c->timeinfo = localtime(&rawtime);
}

/**
 * Uses get_car_time() to print what time the car arrived in the que and what direction
 * @param c - the car added to the queue.
 */
void print_car_arrived(Car* c) {
    get_car_time(c);
    char direction;
    if (c->dir == NORTH) {
        direction = 'N';
    } else {
        direction = 'S';
    }
    printf("Car %d coming from the %c direction arrive in the queue at time %d:%d:%d.\n"
            , c->car_id, direction, c->timeinfo->tm_hour, c->timeinfo->tm_min, c->timeinfo->tm_sec);
}

/**
 * Prints the time the car honked their horn.
 * @param c - The car that honks their horn.
 */
void print_car_honk(Car* c) {
    get_car_time(c);
    char direction;
    if (c->dir == NORTH) {
        direction = 'N';
    } else {
        direction = 'S';
    }
    printf("Car %d coming from the %c direction blew their horn at time %d:%d:%d.\n"
            , c->car_id, direction, c->timeinfo->tm_hour, c->timeinfo->tm_min, c->timeinfo->tm_sec);
}

void print_car_left(Car* c) {
    get_car_time(c);
    char direction;
    if (c->dir == NORTH) {
        direction = 'N';
    } else {
        direction = 'S';
    }
    printf("Car %d coming from the %c direction left the construction zone at time %d:%d:%d.\n"
            , c->car_id, direction, c->timeinfo->tm_hour, c->timeinfo->tm_min, c->timeinfo->tm_sec);
}

/**
* Gets key from user.
* Note: Reused from project 1.
* @return - The key pressed by the user.
*/
char getkey() {
    char key = 0;
    int fD = 0; //fD is the file descriptor. 0 for keyboard input.
    int nfds = 1; //number of file descriptors. 1 since we are only worried about keyboard input.
    fd_set fs; //declare a fd_set named fs

    FD_ZERO(&fs);
    FD_SET(fD, &fs);

    struct timeval time;
    time.tv_sec = 0;
    time.tv_usec = 0;

    //int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
    int numberOfKeysReady = select(nfds, &fs, NULL, NULL, &time); //Look up man pages for select(2)

    //if numberOfKeysReady == 1 read from the file descriptor
    // use read sys call to get the key pressed
    if (numberOfKeysReady > 0)
    {
        //ssize_t read(int fd, void *buf, size_t count);
        read(fD, &key, 1);
    }
    return key; //value of key is the decimal value of the character. See ascii table.
}

//void let_car_pass(struct car_queue* queue) {
//    struct car_queue current_car_queue = *queue;
//    do {
//        down(sems.nb_empty);
//        down(sems.sem_mutex);
//        Car* dequed_car = dequeue(&current_car_queue);
//        let_car_through(); //sleeps for 2 seconds while care passes through construction zone.
//        print_car_left(dequed_car);
//        //gotta check to see if other queue is full. If it is we have to switch sides and consume from the other side
//        if (*current_direction == NORTH) {
//            if (is_full(south_bound)) {
//                //If the opposite side is full we have to change current_direction to let cars from other side go.
//                *current_direction = SOUTH;
//            }
//        } else { //current_direction == South
//            if (is_full(north_bound)) {
//                //If the opposite side is full we have to change current_direction to let cars from other side go.
//                *current_direction = NORTH;
//            }
//        }
//
//        //Change the current_car_queue to dequeue from the current_direction.
//        if (*current_direction == NORTH) {
//            current_car_queue = *north_bound;
//        } else {
//            current_car_queue = *south_bound;
//        }
//    }
//    while (!is_empty(&current_car_queue));
//}

/**
 * Adds cars to a given queue. If the queue is empty and flag_person is asleep the car will wake up the flag_person
 * by honking their horn.
 * @param queue
 * @return Returns zero when the queue is full and 1 if the cars stopped comming but queue is not full.
 */
int car_arrives(struct car_queue* queue, Direction direction, struct cs1550_sem* full_sem, struct cs1550_sem* empty_sem) {
    //produces cars each car created has an 80% chance of a car being behind it.
    do {
        down(empty_sem);
        down(sems.sem_mutex);
        //Create a car
        Car* car = malloc(sizeof(Car));
        car->car_id = *car_id_count;
        car->dir = direction;
        *car_id_count = *car_id_count + 1; //increment car count so next car created has a new id.
        if (is_empty(north_bound) && is_empty(south_bound)) { //check to see if queues are empty. Which means car about to be added will be the first car.
            //first car is arriving
            print_car_honk(car); //Car honks to wake up flag person.
            *current_direction = direction;//Set current direction to the honking direction
        }
        if(is_full(queue)){
            //When the que becomes full set the current_direction to this direction
            // so that the flag_person can start consuming from the full queue.
//            *current_direction = direction;
            if (direction == NORTH) {
                printf("NorthBound Queue is full (line 393)\n");
            } else {
                printf("SouthBound Queue is full (line 395)\n");
            }

            return 0;  //break out of loop (stop producing)
        } else {
            enqueue(queue, car);
        }
        print_car_arrived(car);
        up(sems.sem_mutex);
        up(full_sem);
    }
    while(chance_80());
    delay_20_sec(); //once no car comes there is a 20 second delay before any new car will come.
    return 1;
}

int main(int argc, char **argv) {
    srand(time(NULL));
    char key = getkey();

    //Start by initializing the simulation.
    init_sim();

    //Gets the current process ID. This dictates what code to run (producer or consumer)
    int current_process = fork();

    if (current_process == 0) { //child process
        printf("About to execute northbound producer code\n");
        //Northbound producer
        while(key != 'q') {
            printf("N - car_arrives() called line 426\n");
            int retval = car_arrives(north_bound, NORTH, sems.nb_full, sems.nb_empty);  //car arrives in northbound queue.
            printf("   retVal = %d\n", retval);
            key = getkey();
        }
    } else if (current_process > 0) { //parent process.
        int pid = fork(); //fork again from parent process to create a third process.
        if(pid == 0) { //child2 process.
            //Southbound producer
            printf("About to execute southbound producer code\n");
            while(key != 'q') {
                printf("S - car_arrives() called line 439\n");
                int retval = car_arrives(south_bound, SOUTH, sems.sb_full, sems.sb_empty);//car arrives in southbound queue.
                printf("   retVal = %d\n", retval);
                key = getkey();
            }

        } else if (pid > 0) { //parent process.
            //Flag Person
            // allow cars to travel (consume)
            printf("About to execute flag_person consumer code\n");
            while(key != 'q'){
                if (*current_direction == NORTH) {
                    down(sems.nb_full);
                    down(sems.sem_mutex);
                        //Consume from north queue.
                        Car* dequed_car = dequeue(north_bound);
                        let_car_through(); //sleeps for 2 seconds while care passes through construction zone.
                        print_car_left(dequed_car);
                    //gotta check to see if other queue is full. If it is we have to switch sides and consume from the other side
                    if (is_full(south_bound) || is_empty(north_bound)) {
                        //If the opposite side is full we have to change current_direction to let cars from other side go.
                        *current_direction = SOUTH;
                    }

                    up(sems.sem_mutex);
                    up(sems.nb_empty);
                } else if (*current_direction == SOUTH){
                    down(sems.sb_full);
                    down(sems.sem_mutex);
                        Car* dequed_car = dequeue(south_bound);
                        let_car_through(); //sleeps for 2 seconds while care passes through construction zone.
                        print_car_left(dequed_car);
                        //gotta check to see if other queue is full. If it is we have to switch sides and consume from the other side
                    if (is_full(north_bound) || is_empty(south_bound)) {
                        //If the opposite side is full we have to change current_direction to let cars from other side go.
                        *current_direction = NORTH;
                    }
                    up(sems.sem_mutex);
                    up(sems.sb_empty);
                }
                key = getkey();
            }

        }
    }


    return 0;
}