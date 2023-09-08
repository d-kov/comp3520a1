#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int no_of_customers;
int customers_serviced; //counts how many customers have come and gone
int finished_flag = 0;
int no_of_seats; 
int no_of_free_seats; 
int num_barbers;
int current_ticket = 1; //represents current ticket number
//barber_chairs_tickets is managed by the assistant, but cleared by the customer once they leave
int * barber_chairs_tickets; //array representing which ticket number is assigned to which barber. index refers to barber number, value refers to ticket number. value of 0 indicates empty chair.
//barber_chairs_ids is managed by the assistant, but cleared by the customer once they leave
int * barber_chairs_ids; //array representing which customer ID is assigned to which barber. index refers to barber number, value refers to customer ID. value of 0 indicates empty chair. necessary to know which customer ID signal to send.
int * tickets; //array representing which ticket is assigned to which customer. Index + 1 is ticket number, value is customer ID.
int minimum_barber_pace, maximum_barber_pace; // set up the barber's working time range
int barbers_remaining; //represents how many barbers are left in the store. this is to ensure ALL barbers received the wakeup signal from the assistant.
int * barber_done; //array that holds the conditional variables referring to whether the barber has finished cutting hair or not. Index + 1 refers to barber ID, values of 0 for working, 1 for finished. required to account for spurious wakeups in the customer threads.

void * customer_routine(void *);
void * barber_routine(void *);
void * assistant_routine(void *);

//declare global mutex and condition variables
pthread_mutex_t seats_mutex = PTHREAD_MUTEX_INITIALIZER; //waiting chair mutex
pthread_mutex_t barber_chair_mutex = PTHREAD_MUTEX_INITIALIZER; //barber chair mutex
pthread_mutex_t ticket_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t serviced_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t haircut_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t * customer_condition_variables;
pthread_cond_t * barber_condition_variables; //format: 0th is wait for customer to be allocated, 1st is wait for customer to sit in seat
pthread_cond_t customer_signal_assistant;
pthread_cond_t barber_signal_assistant;

int main(int argc, char ** argv) {
	pthread_t * customer_threads; //customer threads
	pthread_t * barber_threads; //barber threads
	int * customer_ids; //customer thread ids
	int * barber_ids; //barber thread ids
	int minimum_customer_rate, maximum_customer_rate; //set up the customer's arrival pace range
 	int k, rc; //set up k counter and rc for checking threads are created successfully

	// ask for a total number of seats.
	printf("Enter the total number of waiting chairs (int): \n");
	scanf("%d", &no_of_seats);
	
	// ask for the total number of customers.
	printf("Enter the total number of customers (int): \n");
	scanf("%d", &no_of_customers);
	
	//ask for the number of barbers in this salon
	printf("Enter the total number of barbers (int): \n");
	scanf("%d", &num_barbers);

	//initialise barbers remaining variable here
	barbers_remaining = num_barbers;

	//ask for barber's minimum working pace
	printf("Enter minimum barber working pace (int): \n");
	scanf("%d", &minimum_barber_pace);
	
	//ask for barber's maximum working pace
	printf("Enter maximum barber working pace (int): \n");
	scanf("%d", &maximum_barber_pace);

	//ask for customers' minimum arrival rate
	printf("Enter customers minimum arrival rate (int): \n");
	scanf("%d", &minimum_customer_rate);

	//ask for customers' maximum arrival rate
	printf("Enter customers maximum arrival rate (int): \n");
	scanf("%d", &maximum_customer_rate);

	//initialise the barber_done array
	barber_done = malloc(num_barbers * sizeof(int));
	if (barber_done == NULL) {
		fprintf(stderr, "barber done out of memory\n");
		exit(1);
	}
	for (int i = 0; i < num_barbers; i++) {
		barber_done[i] = 1;
	}

	//initialise the customer condition variables.
	customer_condition_variables = malloc((no_of_customers * 2) * sizeof(pthread_cond_t));
	for (int i = 0; i < no_of_customers * 2; i++) {
		rc = pthread_cond_init(&customer_condition_variables[i], NULL);
		if (rc) {
			printf("ERROR; return code from pthread_cond_init() is %d\n", rc);
			exit(-1);
		}
	}

	//initialise the barber condition variables.
	barber_condition_variables = malloc((num_barbers * 2) * sizeof(pthread_cond_t));
	for (int i = 0; i < num_barbers * 2; i++) {
		rc = pthread_cond_init(&barber_condition_variables[i], NULL);
		if (rc) {
			printf("ERROR; return code from pthread_cond_init() is %d\n", rc);
			exit(-1);
		}
	}

	//initialise the assistant condition variables
	rc = pthread_cond_init(&customer_signal_assistant, NULL);
	if (rc) {
		printf("ERROR; return code from pthread_cond_init() is %d\n", rc);
		exit(-1);
	}

	rc = pthread_cond_init(&barber_signal_assistant, NULL);
	if (rc) {
		printf("ERROR; return code from pthread_cond_init() is %d\n", rc);
		exit(-1);
	}
	
	customer_threads = malloc((no_of_customers) * sizeof(pthread_t)); //store all customer threads here
	if(customer_threads == NULL){
		fprintf(stderr, "customer threads out of memory\n");
		exit(1);
	}

	barber_threads = malloc((num_barbers + 1) * sizeof(pthread_t)); //store all barbers + 1 assistant here. NOTE: assistant will always be 1st element in barber array
	if(barber_threads == NULL){
		fprintf(stderr, "barber threads out of memory\n");
		exit(1);
	}

	customer_ids = malloc((no_of_customers) * sizeof(int)); //customer thread ids here
	if(customer_ids == NULL){
		fprintf(stderr, "customer_ids out of memory\n");
		exit(1);
	}

	barber_ids = malloc((no_of_customers) * sizeof(int)); //barbers + assistant thread ids here
	if(barber_ids == NULL){
		fprintf(stderr, "barber_ids out of memory\n");
		exit(1);
	}

	//initialise all the barber chairs tickets to 0, indicating they are available.
	barber_chairs_tickets = malloc((num_barbers) * sizeof(int));
	if (barber_chairs_tickets == NULL) {
		fprintf(stderr, "barber_chairs_tickets out of memory\n");
	}
	for (int i = 0; i < num_barbers; i++) {
		barber_chairs_tickets[i] = 0;
	}

	//initialise all the barber chairs ids to 0, indicating they are available.
	barber_chairs_ids = malloc((num_barbers) * sizeof(int));
	if (barber_chairs_ids == NULL) {
		fprintf(stderr, "barber_chairs_ids out of memory\n");
	}
	for (int i = 0; i < num_barbers; i++) {
		barber_chairs_ids[i] = 0;
	}
	
	//initialise tickets array to 0, indicating that all tickets are initially unassigned
	tickets = malloc((no_of_seats) * sizeof(int));
	if (tickets == NULL) {
		fprintf(stderr, "tickets out of memory\n");
	}
	for (int i = 0; i < no_of_seats; i++) {
		tickets[i] = 0;
	}
	

	//initialize no_of_free_seats.
	no_of_free_seats = no_of_seats;

	//create the assistant thread
	rc = pthread_create(&barber_threads[0], NULL, assistant_routine, NULL);
	if (rc) {
		printf("ERROR; return code from pthread_create() (barber) is %d\n", rc);
		exit(-1);
	}

	//create the barber threads.
	for (k = 1; k <= num_barbers; k++) {
		barber_ids[k] = k;
		rc = pthread_create(&barber_threads[k], NULL, barber_routine, (void *) &barber_ids[k]);
		if (rc) {
			printf("ERROR; return code from pthread_create() (barber %d) is %d\n", k, rc);
			exit(-1);
		}
    }

	//create customers according to the arrival rate
    for (k = 1; k <= no_of_customers; k++) {
		sleep(rand() % (maximum_customer_rate + 1 - minimum_customer_rate) + minimum_customer_rate);
		customer_ids[k] = k;
		rc = pthread_create(&customer_threads[k - 1], NULL, customer_routine, (void *) &customer_ids[k]);
		if (rc) {
			printf("ERROR; return code from pthread_create() (customer %d) is %d\n", k, rc);
			exit(-1);
		}
    }
    
	//join customer threads.
    for (k = 1; k <= no_of_customers; k++) {
        pthread_join(customer_threads[k], NULL);
    }

    printf("Main thread: All customers have now been served. Salon is closed now.\n");
	
	//join the assistant and barber threads
    for (k = 0; k <= num_barbers; k++) {
    	pthread_join(barber_threads[k], NULL);
    }
	
    //destroy mutex and condition variable objects
	pthread_mutex_destroy(&seats_mutex);
	pthread_mutex_destroy(&barber_chair_mutex);
	pthread_mutex_destroy(&ticket_mutex);
	pthread_mutex_destroy(&serviced_mutex);
	for (int i = 0; i < no_of_customers; i++) {
		pthread_cond_destroy(&customer_condition_variables[2 * i]);
		pthread_cond_destroy(&customer_condition_variables[(2 * i) + 1]);	
	}
	for (int i = 0; i < num_barbers; i++) {
		pthread_cond_destroy(&barber_condition_variables[2 * i]);
		pthread_cond_destroy(&barber_condition_variables[(2 * i) + 1]);	
	}
	pthread_cond_destroy(&barber_signal_assistant);
	pthread_cond_destroy(&customer_signal_assistant);

	//deallocate allocated memory
	free(customer_condition_variables);	
	free(barber_condition_variables);	
	free(barber_threads);
	free(customer_threads);
	free(barber_ids);
	free(customer_ids);
	free(barber_chairs_tickets);
	free(barber_chairs_ids);
	free(tickets);
	free(barber_done);
	
    pthread_exit(NULL);
}

void * assistant_routine(void * arg) {

	while (1) {
		//check if there are any customers left to service
		pthread_mutex_lock(&serviced_mutex);
		if (customers_serviced == no_of_customers) {
			pthread_mutex_unlock(&serviced_mutex);
			break;	
		}
		pthread_mutex_unlock(&serviced_mutex);

		//wait for customers
		pthread_mutex_lock(&seats_mutex);
		while (no_of_free_seats == no_of_seats) {
			printf("Assistant: I'm waiting for customers.\n");
			pthread_cond_wait(&customer_signal_assistant, &seats_mutex);
		}
		pthread_mutex_unlock(&seats_mutex);

		//wait for a barber to become available
		int barber_index;
		pthread_mutex_lock(&barber_chair_mutex);
		while (1) {
			int break_flag = 0;
			for (int i = 0; i < num_barbers; i++) {
				if (barber_chairs_tickets[i] == 0) {
					//found a free barber, assign customer to this barber
					barber_index = i + 1;
					break_flag = 1;
					break;
				}
			}
			if (break_flag) {
				break;
			}
			printf("Assistant: I'm waiting for a barber to become available.\n");
			pthread_cond_wait(&barber_signal_assistant, &barber_chair_mutex);
		}
		pthread_mutex_unlock(&barber_chair_mutex);

		//we have the barber we want to assign a customer to, now figure out which customer to assign
		int customer_ticket = 0;
		int customer_id; //not sure if i need this tbh, adding this here just to be safe
		int repeat_customer = 0;
		pthread_mutex_lock(&ticket_mutex);
		pthread_mutex_lock(&barber_chair_mutex);
		for (int i = 0; i < no_of_seats; i++) {
			for (int j = 0; j < num_barbers; j++) {
				if (tickets[i] == barber_chairs_ids[j]) {
					//if in this section, you are looking at a customer who is already being served. continue to next customer
					repeat_customer = 1;
					break;
				}
			}
			//if this is a customer already being served, continue to next customer
			if (repeat_customer) {
				repeat_customer = 0;
				continue;
			}
			//if you get to this point, you have found a free customer to assign. break the loop.
			customer_ticket = i + 1;
			customer_id = tickets[i];
			break;
		}
		pthread_mutex_unlock(&barber_chair_mutex);
		pthread_mutex_unlock(&ticket_mutex);
		printf("Assistant: Assign Customer %d to Barber %d.\n", customer_ticket, barber_index);

		//do the actual assigning now that we've figured out the customer and barber to assign
		pthread_mutex_lock(&barber_chair_mutex);
		barber_chairs_ids[barber_index - 1] = customer_id;
		pthread_mutex_unlock(&barber_chair_mutex);

		//free the seat that the customer is currently occupying, as the barber is sending the customer to the barber
		pthread_mutex_lock(&seats_mutex);
		no_of_free_seats++;
		pthread_mutex_unlock(&seats_mutex);

		//send the signal to the customer that they have been assigned a barber
		pthread_cond_signal(&customer_condition_variables[2 * (customer_id - 1)]);

		//at this point the assistant's work is done, loop again to assign more customers

	}

	//if there are no customers left to service, loop breaks and send an appropriate message + signal to the barbers
	printf("Assistant: Hi barbers. We've finished work for the day. See you all tomorrow!\n");
	//dont need to mutex this part, as it does not matter if the read value is strange - it will only be 0 when all the barbers leave
	finished_flag = 1;
	while (barbers_remaining) {
		for (int i = 0; i < num_barbers; i++) {
			pthread_cond_signal(&barber_condition_variables[2 * i]);
		}
	}

	pthread_exit(NULL);
}

void * barber_routine(void * arg) {
    
	int id = * (int *) arg;
	pthread_cond_t * wait_for_customer_cond = &barber_condition_variables[2 * (id - 1)];
	pthread_cond_t * wait_for_chair_cond = &barber_condition_variables[(2 * (id - 1)) + 1];

    while (1) {
    	printf("Barber %d: I'm now ready to accept a customer.\n", id);
    	pthread_cond_signal(&barber_signal_assistant);
    	
    	//wait for customer to sit in chair, and check for end of the day
    	int break_flag = 0; // this flag is for breaking the loop at the end of the day
    	pthread_mutex_lock(&barber_chair_mutex);
    	while (!barber_chairs_tickets[id - 1]) {
    		pthread_cond_wait(wait_for_customer_cond, &barber_chair_mutex);
    		//check whether we still have work to do
    		if (finished_flag) {
    			break_flag = 1;
    			break;
    		}
    	}
    	pthread_mutex_unlock(&barber_chair_mutex);
    	if (break_flag) {
    		break;
    	}

    	//customer in chair, start cutting hair
    	printf("Barber %d: Hello, Customer %d.\n", id, barber_chairs_tickets[id - 1]);

    	//cut the customer's hair
    	sleep(rand() % (maximum_barber_pace + 1 - minimum_barber_pace) + minimum_barber_pace);

    	//put barber in the done position
    	pthread_mutex_lock(&haircut_mutex);
    	barber_done[id - 1] = 1;
    	pthread_mutex_unlock(&haircut_mutex);
    	//finish cutting the customer's hair, and send a signal to the customer to get out of the chair
    	printf("Barber %d: Finished cutting. Good bye Customer %d.\n", id, barber_chairs_tickets[id - 1]);
    	pthread_cond_signal(&customer_condition_variables[2 * (barber_chairs_ids[id - 1] - 1) + 1]); //barber_chairs_ids contains barber id as index, and customer ids as values. Use customer ids as index for customer condition variables, to get the conditional variable to signal the customer with.

    	//wait for seat to be empty before serving next customer
    	pthread_mutex_lock(&barber_chair_mutex);
    	while (barber_chairs_tickets[id - 1]) {
    		pthread_cond_wait(wait_for_chair_cond, &barber_chair_mutex);
    	}
    	pthread_mutex_unlock(&barber_chair_mutex);

    	//put barber in the working position
    	pthread_mutex_lock(&haircut_mutex);
    	barber_done[id - 1] = 0;
    	pthread_mutex_unlock(&haircut_mutex);
    }

    printf("Barber %d: Thanks Assistant. See you tomorrow!\n", id);
    pthread_mutex_lock(&barber_chair_mutex);
    barbers_remaining--;
    pthread_mutex_unlock(&barber_chair_mutex);

    pthread_exit(NULL);
}

void * customer_routine(void * arg) {
	int ticket_num;
	int barber_id;
	int customer_id = * (int *) arg;
	pthread_cond_t * wait_for_barber_cond = &customer_condition_variables[2 * (customer_id - 1)];
	pthread_cond_t * wait_for_haircut_cond = &customer_condition_variables[2 * (customer_id - 1) + 1];

	//customer arrives at the barber shop
    printf("Customer %d: I have arrived at the barber shop.\n", customer_id);
    
    //check for free seats
    pthread_mutex_lock(&seats_mutex);
    if (no_of_free_seats == 0) {
		printf("Customer %d: Oh no! All seats have been taken and I'll leave now!\n", customer_id);
        pthread_mutex_unlock(&seats_mutex);
        pthread_mutex_lock(&serviced_mutex);
        //a customer that immediately left counts as serviced
        customers_serviced++;
        pthread_mutex_unlock(&serviced_mutex);
        pthread_exit(NULL);
    }

    //take a seat and a ticket
    pthread_mutex_lock(&ticket_mutex);
	printf("Customer %d: I'm lucky to get a free seat and a ticket numbered %d.\n", customer_id, current_ticket);
	
	ticket_num = current_ticket; //take a ticket
	current_ticket++; //increment ticket counter
	//check if the ticket system needs to loop back around
	if (current_ticket > no_of_seats) {
		current_ticket = 1;
	}
	//allocate this customer to the ticket
	tickets[ticket_num - 1] = customer_id;
	pthread_mutex_unlock(&ticket_mutex);

	no_of_free_seats--; //take a seat
	pthread_mutex_unlock(&seats_mutex);
	
	pthread_cond_signal(&customer_signal_assistant); //signal the assistant that a customer is ready
	
	//wait for the assistant to assign you to a barber
	pthread_mutex_lock(&barber_chair_mutex);
	while (1) {
		pthread_cond_wait(wait_for_barber_cond, &barber_chair_mutex);
		int break_flag = 0;
		for (int barber_index = 0; barber_index < num_barbers; barber_index++) {
			if (barber_chairs_ids[barber_index] == customer_id) {
				break_flag = 1;
				barber_id = barber_index + 1;
				barber_chairs_tickets[barber_index] = ticket_num;
				break;
			}
		}
		if (break_flag) {
			break;
		}
	}
	pthread_mutex_unlock(&barber_chair_mutex);

	//go up to the barber, seat has been freed by assistant
	printf("Customer %d: My ticket number %d has been called. Hello, Barber %d.\n", customer_id, ticket_num, barber_id);
	//sit in the barber chair and let barber begin cutting hair
	pthread_cond_t * wait_for_customer_cond = &barber_condition_variables[2 * (barber_id - 1)];
	pthread_cond_signal(wait_for_customer_cond); //sit down and tell the barber I'm ready

	//customer waits while barber completes haircut
	//account for the spurious wakeup that causes
	pthread_mutex_lock(&haircut_mutex);
	while (!barber_done[barber_id - 1]) {
		pthread_cond_wait(wait_for_haircut_cond, &haircut_mutex);
	}
	pthread_mutex_unlock(&haircut_mutex);
	printf("Customer %d:  Well done. Thanks Barber %d. Bye!\n", customer_id, barber_id);
	//customer leaves the chair, clear the ID and the ticket barber_chair arrays, drop the ticket too
	pthread_mutex_lock(&barber_chair_mutex);
	barber_chairs_tickets[barber_id - 1] = 0;
	barber_chairs_ids[barber_id - 1] = 0;
	pthread_mutex_unlock(&barber_chair_mutex);
	pthread_mutex_lock(&ticket_mutex);
	tickets[ticket_num - 1] = 0;
	pthread_mutex_unlock(&ticket_mutex);
	//signal the barber that the chair is empty
	pthread_cond_signal(&barber_condition_variables[2 * (barber_id - 1) + 1]);
	//make sure to increment serviced customer counter
	pthread_mutex_lock(&serviced_mutex);
	customers_serviced++;
	pthread_mutex_unlock(&serviced_mutex);

    pthread_exit(NULL);
}
