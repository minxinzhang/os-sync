#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/*
implement a FIFO queue;
A few interfaces are borrowed from W3School tutorial
*/
struct Queue {
	int front, rear, size;
	int capacity;
	int* array;
};

/*queue initialization*/
struct Queue* createQueue(int capacity)
{
	struct Queue* queue = (struct Queue*)malloc(
		sizeof(struct Queue));

	queue->capacity = capacity;
	queue->front = queue->size = 0;

	queue->rear = capacity - 1;
	queue->array = (int*)malloc(queue->capacity * sizeof(int));
    memset(queue -> array, 0, capacity * sizeof(int));
	return queue;
}

/*helper functions section start*/
int isFull(struct Queue* queue) {
	return (queue->size == queue->capacity);
}

int isEmpty(struct Queue* queue) {
	return (queue->size == 0);
}

int enqueue(struct Queue* queue, int id) {
	if (isFull(queue))
		return -1;
	queue->rear = (queue->rear + 1)
				% queue->capacity;
	queue->array[queue->rear] = id;
	queue->size = queue->size + 1;
    return queue->rear;
}

int dequeue(struct Queue* queue) {
	if (isEmpty(queue))
		return 0;
	int item = queue->array[queue->front];
	queue->front = (queue->front + 1) % queue->capacity;
	queue->size = queue->size - 1;
	return item;
}

int frontIndex(struct Queue* queue) {
    return queue -> front + 1;
}
/*helper functions section end*/

/*reserve procedural call names*/
void* barber_routine(void*);
void* customer_routine(void*);
void* assistant_routine(void*);
void readParams();
void initMutexConds();
void destroyMutexConds();

/*parameter args*/
int M; //customer total #
int K; //barber total #
int N; //queue capacity
int T1; //barber working rate lower bound
int T2; //baber working rate upper bound
int T3; //customer arriving rate lower bound
int T4; //customer arriving rate upper bound

/*shared resources and flags for preventing 
spuriously waking up `pthread_cond_wait()`*/
struct Queue* c_q; //waiting room queue
struct Queue* b_q; //available barber queue



int to_shut_down = 0;
int m = 0; // # of the customer having arrived
int nextCustomerID = 0; //next custormer to pair with 
int nextBarberID = 0; //next barber to pair with
int nextTicketNumber; //the queue number assigned to the paired customer
int barber_no_work = 0; //# of barbers not working at the moment
int m_leaving = 0; //# of customers who have left
int* cuts; //pipelines for signaling customers

/*reserve synchronization variables*/
pthread_mutex_t count_customers, c_q_lock, b_q_lock, pair_lock, cuts_lock, count_barbers;
pthread_cond_t shutdown, barber_pair, barber_has_no_work, a_customer_leave,
customer_pair, customer_arrive, barber_ready, cut_done;



int main(int argc, char* argv[]) {

    readParams();//read terminal args
    initMutexConds();//initialize locks and conditional variables

    pthread_t* threads; //threads array
    int* b_ids;//barber ids to print
    int* c_ids;//customer ids to print


    //prepare customer and barber FIFO queues
    c_q = createQueue(N);
    b_q = createQueue(K);

    //reserve heap space
    threads = malloc((M + K + 1) * sizeof(pthread_t));
    if(threads == NULL){
		fprintf(stderr, "threads out of memory\n");
		exit(1);
	}
    b_ids = malloc((K + 1) * sizeof(int));
    c_ids = malloc((M + 1) * sizeof(int));
    cuts = malloc((M + 1) * sizeof(int));
    for (int i = 0; i < M + 1; i ++) {
        cuts[i] = 0;
    }

    //init assistant thread
    pthread_create(&threads[0], NULL, assistant_routine, (void *) NULL); 

    //int barber threads
    for (int k = 1; k < K + 1; k++) {
        b_ids[k] = k;
        pthread_create(&threads[k], NULL, barber_routine,(void*) &b_ids[k]); 
    }

    //init customer threads
    srand(time(0)); //r seed for customer arriving rate
    for (int k = 1; k < M + 1; k++) {
        c_ids[k] = k;
        sleep((rand() % (T4 - T3 + 1)) + T3); // customers arrive at a random interval
        pthread_create(&threads[K + k], NULL, customer_routine,(void*) &c_ids[k]); 
    }    


    //wait for barber threads and customer threads terminate
    for (int k = 0; k < K + M + 1; k++) {
        pthread_join(threads[k], NULL);      
    }

    //free heap
    free(threads);
    free(b_ids);
    free(c_ids);
    free(cuts);

    destroyMutexConds();// destroy all locks and conditional variables

    pthread_exit(0);
}

void* barber_routine(void* arg) {
    int* id_ptr = (int*) arg;
    int id = *id_ptr; //barber local id
    srand(time(0)*(id+1)); //barber working pace random seed
    int servingCustomer;//local customer id barber is serving

    while (1) { 

        /*
        A barber has no customer to serve, puts himself in the avaiable
        barber queue and notifies assistant that he's ready to serve
        */
        pthread_mutex_lock(&b_q_lock);
        printf("Barber [%d]: I'm now ready to accept a customer\n",id);
        enqueue(b_q,id);//queue
        pthread_cond_signal(&barber_ready);//notify assistant
        pthread_mutex_unlock(&b_q_lock);

        /*
        The barber waits for a customer calling him to serve
        */
        pthread_mutex_lock(&pair_lock);
        while (id != nextBarberID) {//barber seat empty
            pthread_cond_wait(&barber_pair, &pair_lock);//wait for the customer call

            /*
            The following section is for assistant thread to wake up barber threads waiting 
            for customers. When barber threads are awakened this way, they send feedbacks to
            the assistant thread that they have been aware that they will wait for the instruction
            from the assistant to close the shop.
            */
            if (nextBarberID == -1) {
                pthread_mutex_unlock(&pair_lock);
                pthread_mutex_lock(&count_barbers);
                barber_no_work += 1;
                pthread_cond_signal(&barber_has_no_work);
                /*
                wait for the shop closing protocol.
                */
                while(!to_shut_down) {
                    pthread_cond_wait(&shutdown,&count_barbers);
                    printf("Barber [%d]: Thanks Assistant. See you tomorrow!\n", id);
                    pthread_mutex_unlock(&count_barbers);
                    pthread_exit(NULL); 
                }
                pthread_mutex_unlock(&count_barbers);
            }
            
        }

        printf("Barber [%d]: Hello, Customer [%d] with ticket number {%d}\n", id, nextCustomerID, nextTicketNumber);
        /*pairing has been completed
        reset pairing params*/
        servingCustomer = nextCustomerID;
        nextBarberID = 0;
        nextCustomerID = 0;
        //serving
        pthread_mutex_unlock(&pair_lock);
        sleep((rand() % (T2 - T1 + 1)) + T1);
        pthread_mutex_lock(&cuts_lock);
        cuts[servingCustomer] = 1;
        printf("Barber [%d]: Finished cutting. Good bye, customer [%d].\n", id, servingCustomer);
        pthread_cond_broadcast(&cut_done);//notify customer
        pthread_mutex_unlock(&cuts_lock);
    }
    return NULL;
}

void* customer_routine(void* arg) {
    int* id_ptr = (int*) arg;
    int id = *id_ptr;

    pthread_mutex_lock(&count_customers);
    m++;//for counting customers arriving at the shop
    printf("Customer [%d]: I have arrived at the barber shop.\n",id);
    pthread_mutex_unlock(&count_customers);

    pthread_mutex_lock(&c_q_lock);
    if (isFull(c_q)) { //full room
        printf("Customer [%d]: oh no! all seats have been taken and I'll leave now!\n", id);
    } else {
        //customer gets in q: enqueue
        int ticketNumber = enqueue(c_q,id) + 1;
        printf("Customer [%d]: I'm lucky to get a free seat and a ticket numbered {%d}\n", id, ticketNumber);
        
        pthread_cond_signal(&customer_arrive);//tell the assistant the room is not empty
        pthread_mutex_unlock(&c_q_lock);

        pthread_mutex_lock(&pair_lock);
        while (id != nextCustomerID) { //not called
            pthread_cond_wait(&customer_pair, &pair_lock);//wait for assistant to call
        }
        printf("Customer [%d]: My ticket numbered {%d} has been called. Hello, Barber [%d]!\n", id, nextTicketNumber, nextBarberID);
        int servingBarber = nextBarberID;
        pthread_cond_broadcast(&barber_pair);//notify barber
        pthread_mutex_unlock(&pair_lock);

        pthread_mutex_lock(&cuts_lock);
        while (cuts[id] != 1) { //not signaled
            pthread_cond_wait(&cut_done, &cuts_lock);//wait for barber to get cut done
        }
        pthread_mutex_unlock(&cuts_lock);

        printf("Customer [%d]: Well done. Thank barber[%d], bye!\n",id, servingBarber);

    }
    pthread_mutex_unlock(&c_q_lock);


    pthread_mutex_lock(&count_customers);
    m_leaving++;//for customers leaving the shop.
    pthread_cond_signal(&a_customer_leave);
    pthread_mutex_unlock(&count_customers);

    pthread_exit(NULL);
    return NULL;
}


void* assistant_routine(void* arg) {
    while(1) {

        pthread_mutex_lock(&count_customers);
        
        /*
        accepts new customers
        */
        if (m < M) {
            pthread_mutex_unlock(&count_customers);
            pthread_mutex_lock(&c_q_lock);
            while(isEmpty(c_q)) {//no customer in room
                printf("Assistant: I\'m waiting for customers.\n");
                pthread_cond_wait(&customer_arrive, &c_q_lock);//wait for customer to arrive
            }
            pthread_mutex_unlock(&c_q_lock);

            pthread_mutex_lock(&b_q_lock);
            while(isEmpty(b_q)) { // all barbers busy
                printf("Assistant: I\'m waiting for barber to become available.\n");
                pthread_cond_wait(&barber_ready, &b_q_lock);//wait for barber's call
            }
            pthread_mutex_unlock(&b_q_lock);

            //start to pair a customer with a barber
            //pair procedure is atomic
            pthread_mutex_lock(&c_q_lock);
            pthread_mutex_lock(&b_q_lock);
            pthread_mutex_lock(&pair_lock);
            nextTicketNumber = frontIndex(c_q);
            nextCustomerID = dequeue(c_q);
            nextBarberID = dequeue(b_q);
            pthread_mutex_unlock(&c_q_lock);
            pthread_mutex_unlock(&b_q_lock);


            printf("Assistant: Assign Customer [%d] with ticket number {%d} to Barber [%d].\n",nextCustomerID,nextTicketNumber,nextBarberID);
            //pairing done, let customer and barber handle the rest work
            pthread_cond_broadcast(&customer_pair); 
            pthread_mutex_unlock(&pair_lock);
        } 

        /*
        no longer waits for new customers
        instead, waits all exisiting customers to 
        get hair cut done
        */
        else {
            pthread_mutex_lock(&c_q_lock);
            if (isEmpty(c_q)) {
                pthread_mutex_unlock(&c_q_lock);
                //wait for all existing customers leaving
                while (m_leaving != M) {
                    pthread_cond_wait(&a_customer_leave, &count_customers);
                }
                pthread_mutex_unlock(&count_customers);

                /*
                notify all barbers they don't need to wait for customers
                */
                pthread_mutex_lock(&pair_lock);
                nextBarberID = -1;
                pthread_cond_broadcast(&barber_pair);
                pthread_mutex_unlock(&pair_lock);

                /*
                wait for all barbers sending back message that they have
                all received the previous notification
                */
                pthread_mutex_lock(&count_barbers);
                while (barber_no_work != K) {
                    pthread_cond_wait(&barber_has_no_work, &count_barbers);
                }
                pthread_mutex_unlock(&count_barbers);

                /*
                enters shop termination protocol
                */
                printf("Assistant: Hi Barber, we\'ve finished the work for the day.\n");
                to_shut_down = 1;
                pthread_cond_broadcast(&shutdown);//notify all barbers
                pthread_mutex_unlock(&count_barbers);
                pthread_exit(NULL);
            }

            else {
                
                pthread_mutex_unlock(&c_q_lock);
                pthread_mutex_lock(&b_q_lock);
                while(isEmpty(b_q)) { // all barbers busy
                    printf("Assistant: I\'m waiting for barber to become available.\n");
                    pthread_cond_wait(&barber_ready, &b_q_lock);//wait for barber's call
                }
                pthread_mutex_unlock(&b_q_lock);

                //start to pair a customer with a barber
                //pair procedure is atomic
                pthread_mutex_lock(&c_q_lock);
                pthread_mutex_lock(&b_q_lock);
                pthread_mutex_lock(&pair_lock);
                nextTicketNumber = frontIndex(c_q);
                nextCustomerID = dequeue(c_q);
                nextBarberID = dequeue(b_q);
                pthread_mutex_unlock(&c_q_lock);
                pthread_mutex_unlock(&b_q_lock);


                printf("Assistant: Assign Customer [%d] with ticket number {%d} to Barber [%d].\n",nextCustomerID,nextTicketNumber,nextBarberID);
                //pairing done, let customer and barber handle the rest work
                pthread_cond_broadcast(&customer_pair); 
                pthread_mutex_unlock(&pair_lock);
            }
            
        } 
        pthread_mutex_unlock(&count_customers); 
    }
}

void readParams() {
    printf("Enter the number of the seats (int): ");
	scanf("%d", &N);	
    printf("Enter the total number of barbers (int): ");
	scanf("%d", &K);
    printf("Enter the total number of customers (int): ");
	scanf("%d", &M);
	printf("Enter barber's working lower pace (int): ");
	scanf("%d", &T1);
    printf("Enter barber's working upper pace (int): ");
	scanf("%d", &T2);
	printf("Enter customer's arriving lower rate (int): ");
	scanf("%d", &T3);
    printf("Enter customer's arriving upper pace (int): ");
	scanf("%d", &T4);
}

void initMutexConds() {
    pthread_mutex_init(&count_customers,NULL);
    pthread_mutex_init(&c_q_lock,NULL);
    pthread_mutex_init(&b_q_lock,NULL);
    pthread_mutex_init(&pair_lock,NULL);
    pthread_mutex_init(&cuts_lock,NULL);
    pthread_mutex_init(&count_barbers,NULL);
    pthread_cond_init(&shutdown,NULL);
    pthread_cond_init(&barber_pair,NULL);
    pthread_cond_init(&customer_pair,NULL);
    pthread_cond_init(&customer_arrive,NULL);
    pthread_cond_init(&barber_ready,NULL);
    pthread_cond_init(&cut_done,NULL);
    pthread_cond_init(&barber_has_no_work, NULL);
    pthread_cond_init(&a_customer_leave,NULL);
}

void destroyMutexConds() {
    pthread_mutex_destroy(&count_customers);
    pthread_mutex_destroy(&c_q_lock);
    pthread_mutex_destroy(&b_q_lock);
    pthread_mutex_destroy(&pair_lock);
    pthread_mutex_destroy(&cuts_lock);
    pthread_mutex_destroy(&count_barbers);
    pthread_cond_destroy(&shutdown);
    pthread_cond_destroy(&barber_pair);
    pthread_cond_destroy(&customer_pair);
    pthread_cond_destroy(&customer_arrive);
    pthread_cond_destroy(&barber_ready);
    pthread_cond_destroy(&cut_done);
    pthread_cond_destroy(&barber_has_no_work);
    pthread_cond_destroy(&a_customer_leave); 
}