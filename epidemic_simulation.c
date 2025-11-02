#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

// DEFINE AREA ----------------------------------------------------------------

// #define DEBUG // comment to prevent prints on all steps

// to convert from nano seconds to seconds
#define NANO 1000000000.0

#define CARDINAL_N 0
#define CARDINAL_S 1
#define CARDINAL_E 2
#define CARDINAL_W 3

// whether a person's movement pattern is 
// vertical or horizontal
#define DIR_VERTICAL 0
#define DIR_HORIZONTAL 1

// the status the person is in
#define STAT_INFECTED 0
#define STAT_SUSCEPTIBLE 1
#define STAT_IMMUNE 2

// duration of infected and immune duration
#define TIME_INFECTED 3
#define TIME_IMMUNE 3

// buffer size for reading the file
#define BUFFER_SIZE 100

typedef struct person_t {
    int id;
    int x, y;
    int movement_pattern, amplitude;
    int status, got_infected;
    int timer_infected, timer_immune;
    int count_infected;
} person_t;


// GLOBAL VARIABLES -----------------------------------------------------------

int simulation_time;
int thread_number;
FILE* input_file;
char* file_name;
int max_coord_x, max_coord_y;
int people_number;
// arrays that will contain the address of the people
person_t **people_serial, **people_parallel;
pthread_barrier_t barrier;

// PERSON STRUCTURE FUNCTIONS -------------------------------------------------

person_t* generate_person(int id, int x, int y, int init_status, int pattern, int amplitude) {
    person_t* person = malloc(sizeof(person_t));
    if(person == NULL) {
        printf("Not enough memory to declare another person.\n");
    }
    
    person->id = id;
    if(x < 0 || y < 0 || x > max_coord_x || y > max_coord_y) {
        printf("Coordinates are out of bounds.\n");
        return NULL;
    }
    person->x = x; person->y = y;

    // the direction will be translated as follows:
    // N - vertical, amplitude negative
    // S - vertical, amplitude positive
    // E - horizontal, amplitude positive
    // W - horizontal, amplitude negative
    switch(pattern) {
        case CARDINAL_N:
            person->movement_pattern = DIR_VERTICAL; person->amplitude = -1 * amplitude;
            break;
        case CARDINAL_S:
            person->movement_pattern = DIR_VERTICAL; person->amplitude = amplitude;
            break;
        case CARDINAL_E:
            person->movement_pattern = DIR_HORIZONTAL; person->amplitude = amplitude;
            break;
        case CARDINAL_W:
            person->movement_pattern = DIR_HORIZONTAL; person->amplitude = -1 * amplitude;
            break;
        default:
            printf("Undefined movement pattern.\n");
            return NULL;
    }

    person->status = init_status;
    // timers are set to max value and count down while person is infected/immune
    person->timer_infected = TIME_INFECTED; person->timer_immune = TIME_IMMUNE;
    person->count_infected = 0;

    return person;
}

// FUNCTIONS FOR HANDLING ARGUMENTS/INPUTS ------------------------------------

void handle_arguments(char* argv[]) {
    simulation_time = atoi(argv[1]);
    if(simulation_time <= 0) {
        printf("Incorrect simulation time value.\n");
        exit(-2);
    }

    input_file = fopen(argv[2], "r");
    file_name = malloc(sizeof(char) * strlen(argv[2]));
    strncpy(file_name, argv[2], strlen(argv[2]));
    if(input_file == NULL) {
        printf("Error opening the file.\n");
        exit(-2);
    }

    thread_number = atoi(argv[1]);
    if(thread_number <= 0) {
        printf("Incorrect thread number value.\n");
        exit(-2);
    }
}

person_t* get_person_data_from_string(char* string, int line) {
    char* strtok_pointer;
    int id, x, y, status, pattern, amplitude;
    errno = 0;
    strtok_pointer = strtok(string, " "); id = atoi(strtok_pointer);
    strtok_pointer = strtok(NULL, " "); x = strtod(strtok_pointer, NULL);
    strtok_pointer = strtok(NULL, " "); y = strtod(strtok_pointer, NULL);
    strtok_pointer = strtok(NULL, " "); status = strtod(strtok_pointer, NULL);
    strtok_pointer = strtok(NULL, " "); pattern = strtod(strtok_pointer, NULL);
    strtok_pointer = strtok(NULL, " "); amplitude = atoi(strtok_pointer);

    if(id <= 0 || amplitude <= 0 || errno != 0) {
        printf("Error parsing person data at line: %d\n", line);
        exit(-4);
    }

    return generate_person(id, x, y, status, pattern, amplitude);
}

// presume file is already opened inside the input file
void read_input_from_file() {
    char* buffer = malloc(BUFFER_SIZE * sizeof(char));
    char* buffer_copy = malloc(BUFFER_SIZE * sizeof(char));
    char* strtok_pointer;

    // read max dimensions of rectangle
    fgets(buffer, BUFFER_SIZE, input_file);
    strtok_pointer = strtok(buffer, " "); max_coord_x = atoi(strtok_pointer);
    strtok_pointer = strtok(NULL, " "); max_coord_y = atoi(strtok_pointer);
    if(max_coord_x <= 0 || max_coord_y <= 0) {
        printf("Error reading the max coordinates.\n");
        exit(-3);
    }


    // read number of people
    fgets(buffer, BUFFER_SIZE, input_file);
    people_number = atoi(buffer);
    if(people_number <= 0) {
        printf("Error reading the number of people.\n");
        exit(-3);
    }

    // read person data
    people_serial = malloc(people_number * sizeof(person_t));
    people_parallel = malloc(people_number * sizeof(person_t));
    for(int i = 0; i < people_number; i++) {
        fgets(buffer, BUFFER_SIZE, input_file);
        strcpy(buffer_copy, buffer);
        people_serial[i] = get_person_data_from_string(buffer, i);
        people_parallel[i] = get_person_data_from_string(buffer_copy, i);
    }

    free(buffer);
    free(buffer_copy);
}

void cleanup() {
    fclose(input_file);
    free(file_name);
    for(int i = 0; i < people_number; i++) {
        free(people_serial[i]);
        free(people_parallel[i]);
    }
    free(people_serial);
    free(people_parallel);
}

// HELPER FUNCTIONS -----------------------------------------------------------

void update_position(person_t* person) {
    int new_x = person->x;
    int new_y = person->y;
    int amplitude = person->amplitude;

    if(person->movement_pattern == DIR_VERTICAL) new_y += amplitude;
    else new_x += amplitude;

    if(new_y < 0) {
        new_y = 0 ;
        amplitude *= -1;
    }

    if(new_y >= max_coord_y) {
        new_y = max_coord_y - 1;
        amplitude *= -1;
    }

    if(new_x < 0) {
        new_x = 0;
        amplitude *= -1;
    }

    if(new_x > max_coord_x) {
        new_x = max_coord_x - 1;
        amplitude *= -1;
    }

    person->x = new_x;
    person->y = new_y;
    person->amplitude = amplitude;
}

void infect_neighbors(person_t* infected_person, person_t** people) {
    for(int i = 0; i < people_number; i++)
        // find uninfected people with the same coordinates, but make sure id is different
        if(people[i]->x == infected_person->x && people[i]->y == infected_person->y 
           && people[i]->id != infected_person->id && people[i]->status == STAT_SUSCEPTIBLE)
                people[i]->got_infected = 1;
}

void set_next_status(person_t* person) {
    if(person->status == STAT_SUSCEPTIBLE && person->got_infected) {
        person->status = STAT_INFECTED;
        person->timer_infected = TIME_INFECTED;
        person->count_infected++;
        person->got_infected = 0; 
    } else if(person->status == STAT_INFECTED) {
        person->timer_infected--;
        if(person->timer_infected == 0) {
            person->status = STAT_IMMUNE;
            person->timer_immune = TIME_IMMUNE;
        }
    } else if(person->status == STAT_IMMUNE) {
        person->timer_immune--;
        if(person->timer_immune == 0)
            person->status = STAT_SUSCEPTIBLE;
    }    
}

void print_person_data(person_t* person) {
    char status[15] = "";

    switch(person->status) {
        case STAT_SUSCEPTIBLE:
            strcpy(status, "SUSCEPTIBLE");
            break;
        case STAT_INFECTED:
            strcpy(status, "INFECTED");
            break;
        case STAT_IMMUNE:
            strcpy(status, "IMMUNE");
            break;
    }

    printf("Person %d: (%d, %d), status: %s, was infected %d time(s).\n", person->id, person->x, person->y, status, person->count_infected);
}

void write_result_in_file(char* append, person_t** people) {
    char* file_name_no_extension = strtok(file_name, ".");
    char* new_file_name = malloc(sizeof(char) * (strlen(file_name_no_extension) + strlen(append)));
    char status[15] = "";
    FILE* write_file;

    strcpy(new_file_name, file_name_no_extension);
    strcat(new_file_name, append);
    write_file = fopen(new_file_name, "w");

    if(write_file == NULL) {
        printf("Error creating output file.\n");
        return;
    }

    for(int i = 0; i < people_number; i++) {
        switch(people[i]->status) {
            case STAT_SUSCEPTIBLE:
                strcpy(status, "SUSCEPTIBLE");
                break;
            case STAT_INFECTED:
                strcpy(status, "INFECTED");
                break;
            case STAT_IMMUNE:
                strcpy(status, "IMMUNE");
                break;
        }
        fprintf(write_file, "Person %d: (%d, %d), status: %s, was infected %d time(s).\n", people[i]->id, people[i]->x, people[i]->y, status, people[i]->count_infected);
    }
    printf("Results printed in file: %s\n", new_file_name);
    free(new_file_name);
    fclose(write_file);
}

int check_equal(person_t* p1, person_t* p2) {
    if(p1->id != p2->id) return 0;
    if(p1->x != p2->x) return 0;
    if(p1->y != p2->y) return 0;
    if(p1->status != p2->status) return 0;
    if(p1->count_infected != p2->count_infected) return 0;

    return 1;
}

int check_if_same_result() {
    for(int i = 0; i < people_number; i++)
        if(!check_equal(people_parallel[i], people_serial[i])) {
            return i;
        }
    return -1;
}

// HELPER FUNCTIONS PARALLEL --------------------------------------------------

void* pthread_person_simulate(void* thread_rank) {
    int rank = *(int*)thread_rank;
    int start = (people_number / thread_number) * rank;
    int end;

    if(rank == thread_number - 1) 
        end = people_number - 1;
    else 
        end = (people_number / thread_number) * (rank + 1);
    
    for(int time = 0; time < simulation_time; time++) {
        for(int i = start; i < end; i++) 
            update_position(people_parallel[i]);

        pthread_barrier_wait(&barrier);

        for(int i = start; i < end; i++) 
            if(people_parallel[i]->status == STAT_INFECTED)
                infect_neighbors(people_parallel[i], people_parallel);
        
        pthread_barrier_wait(&barrier);
        
        for(int i = start; i < end; i++) 
            set_next_status(people_parallel[i]);

#ifdef DEBUG
        for(int i = start; i < end; i++)
            print_person_data(people_parallel[i]);
        printf("\n");
#endif
        pthread_barrier_wait(&barrier);
    }

    return NULL;
}

// SIMULATION -----------------------------------------------------------------

void epidemic_simulation_serial() {
    for(int time = 0; time < simulation_time; time++) {
        
        for(int i = 0; i < people_number; i++) 
            update_position(people_serial[i]);

        // check only people that are infected, as only they can propagate
        // their status.
        for(int i = 0; i < people_number; i++)
            if(people_serial[i]->status == STAT_INFECTED)
                infect_neighbors(people_serial[i], people_serial);

        for(int i = 0; i < people_number; i++) 
            set_next_status(people_serial[i]);

#ifdef DEBUG
        for(int i = 0; i < people_number; i++)
            print_person_data(people_serial[i]);
        printf("\n");
#endif
    }
}

void epidemic_simulation_parallel() {
    pthread_t* threads = malloc(sizeof(pthread_t) * thread_number);
    int* tid = malloc(sizeof(int) * thread_number);
    pthread_barrier_init(&barrier, NULL, thread_number);

    for(int thread = 0; thread < thread_number; thread++) {
        tid[thread] = thread;
        pthread_create(&threads[thread], NULL, pthread_person_simulate, &tid[thread]);
    }

    for(int thread = 0; thread < thread_number; thread++) {
        pthread_join(threads[thread], NULL);
    }

    pthread_barrier_destroy(&barrier);
    free(threads);
    free(tid);
}

// MAIN -----------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if(argc < 3) {
        printf("Please provide the following arguments: simulation time, input file name, thread number.\n");
        return -1;
    }

    struct timespec start;
    struct timespec end;
    double time_serial, time_parallel;

    handle_arguments(argv);
    read_input_from_file();

    clock_gettime(CLOCK_MONOTONIC, &start);
    epidemic_simulation_serial();
    clock_gettime(CLOCK_MONOTONIC, &end);
    time_serial = end.tv_sec - start.tv_sec;
    time_serial += (end.tv_nsec - start.tv_nsec) / NANO;
    printf("Time for serial: %lf\n", time_serial);
    write_result_in_file("_serial_out.txt", people_serial);

    clock_gettime(CLOCK_MONOTONIC, &start);
    epidemic_simulation_parallel();
    clock_gettime(CLOCK_MONOTONIC, &end);
    time_parallel = end.tv_sec - start.tv_sec;
    time_parallel += (end.tv_nsec - start.tv_nsec) / NANO;
    printf("Time for parallel: %lf\n", time_parallel);
    write_result_in_file("_parallel_out.txt", people_parallel);

    printf("Speedup: %lf\n", time_serial / time_parallel);

    int id_of_mismatch = check_if_same_result();
    if(id_of_mismatch == -1) printf("Results match.\n");
    else printf("!!! RESULTS DO NOT MATCH AT %d !!!\n", id_of_mismatch);
    
    cleanup();
    return 0;
}