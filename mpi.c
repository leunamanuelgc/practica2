#include <mpi.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define MAX_CLIENTS 15
#define OPEN 1
#define CLOSED 0
#define FREE 1
#define OCCUPIED 0
#define MASTER 0

typedef struct client{
    int id;
    int sleep;
    char priority;
    char sent;
} client;

struct checkout{
    char active;
    char free;
    char priority;
};

void attend_client();
int check_clients_size(struct checkout* checkouts, int nActive, int attending, int pending, int completed, int size);
void reset_clients(struct client* clients);
void reset_client(struct client* client);
void initialize_clients(struct client* clients);
void openclose_priorcheckouts(struct checkout* checkouts, int nActive);
int check_prior(struct client* clients, int nAttend);
void order_clients(struct client* clients, int nAttend, int pending);
void print_clients(struct client* clients, int nAttend, int pending);

int main(int argc, char** argv){
    int rank, /*rango*/ size, /*nº procesos*/ i, /*iterador*/
        attending, /*nº clientes atendiéndose*/
        pending, /*nº clientes pendientes*/
        nAttend, /*nº clientes atendidos*/
        completed, /*nº clientes completados*/
        flag, /*true si MPI_Irecv recibe un mensaje*/
        nActive, /*nº cajas activas*/
        prior_index;    /*indice de cajas prioritarias para la funcion check_prior*/

    struct client* clients; //clientes

    struct client results;  //variable que guarda el mensaje de la caja que ha enviado el mensaje
    struct checkout* checkouts; //cajas

    MPI_Request* request;   //request para saber si se ha recibido un mensaje
    MPI_Status status;      //variable utilizada para conocer quién envió el mensaje

    //Inicialización de comunicación
    MPI_Init(NULL,NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    if(size<2){
        fprintf(stderr, "World size must be greater than 1 for %s\n", argv[0]);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    
    if(rank==0){    //MAESTRO
        srand(time(NULL));
        //Asignando memoria
        clients = (struct client*)malloc(sizeof(struct client)*MAX_CLIENTS);
        checkouts = (struct checkout*)malloc(sizeof(struct checkout)*(size-1));
        request=(MPI_Request*)malloc(sizeof(MPI_Request)*(size-1));
        /*
        INICIALIZANDO
        */
        pending = MAX_CLIENTS;
        completed = 0;
        attending = 0;
        nAttend = 0;
        nActive = ceil(((double)size-1)/2);
        for(i=0;i<size-1;i++){
            checkouts[i].active = OPEN;
            checkouts[i].free = FREE;
        }
        nActive=check_clients_size(checkouts, nActive, attending, pending, completed, size);
        initialize_clients(clients);
        openclose_priorcheckouts(checkouts,nActive);
        /*
        PRINTS INICIALES
        */
        // for(i=0;i<size-1;i++){
        //     printf("checkout %d active: %c free: %c prioritary: %c\n",
        //         i+1, checkouts[i].active?'O':'X', checkouts[i].free?'O':'X', checkouts[i].priority?'O':'X');
        // }
        printf("\n\n");
        print_clients(clients, nAttend, pending);
        //printf("n process=%d, max_clients in queue=%d, master=%d, checkouts=%d, ", size, MAX_CLIENTS, MASTER, (size-1));
        //printf("completed: %d, pending: %d, attending: %d, nAttend: %d, open: %d\n\n\n",completed, pending, attending, nAttend, nActive);
        
        
        //Mandando mensajes
        for(i=0;i<nActive;i++){
            if(checkouts[i].active){
                nActive=check_clients_size(checkouts, nActive, attending, pending, completed, size);
                openclose_priorcheckouts(checkouts,nActive);
                printf("--------------------------- CHECKOUT %d ---------------------------\n", i+1);
                if(checkouts[i].priority){
                    prior_index = check_prior(clients, nAttend);
                    MPI_Send(&clients[prior_index%MAX_CLIENTS],sizeof(client),MPI_CHAR,i+1,0,MPI_COMM_WORLD);
                    clients[prior_index%MAX_CLIENTS].sent = 1;
                    printf("|%d|<-client %d, %d s\n", i+1,prior_index%MAX_CLIENTS, clients[prior_index%MAX_CLIENTS].sleep);
                    printf("|prioritary|\n");
                } else{
                    while(clients[nAttend%MAX_CLIENTS].sent) nAttend++; //Comprueba que el cliente a enviar no se ha enviado ya (puede pasar si un prioritario ha sido enviado)
                    //Se envia el cliente a la caja
                    MPI_Send(&clients[nAttend%MAX_CLIENTS],sizeof(client),MPI_CHAR,i+1,0,MPI_COMM_WORLD);
                    printf("|%d|<-client %d, %d s\n", i+1,nAttend, clients[nAttend%MAX_CLIENTS].sleep);
                    nAttend++;
                }
                checkouts[i].free = OCCUPIED;
                pending--;
                attending++;
                
                // printf("checkout %d active: %c free: %c prioritary: %c\n",
                //                 i+1, checkouts[i].active?'O':'X', checkouts[i].free?'O':'X', checkouts[i].priority?'O':'X');
                printf("Completed: %d. Pending: %d. Attending: %d. nAttend: %d. Open: %d\n",completed, pending, attending, nAttend, nActive);
                printf("-------------------------------------------------------------------\n\n");
                //Función no bloqueante que recibe el resultado de la caja
                MPI_Irecv(&results, sizeof(client),MPI_CHAR, i+1, 0, MPI_COMM_WORLD, &request[i]);
            }
            
        }
    
    } else if (rank > 0) {  //ESCLAVO
        attend_client();
    }

    while(1){
        if(rank == 0){  //MAESTRO
            for(i=1;i<size;i++){
            nActive=check_clients_size(checkouts, nActive, attending, pending, completed, size);
            openclose_priorcheckouts(checkouts,nActive);
                //comprobamos si la caja está activa y ocupada
                if(checkouts[i-1].active && !checkouts[i-1].free){
                    //comprobamos si la caja ha enviado un mensaje
                    MPI_Test(&request[i-1],&flag,&status);
                    if(flag){
                        printf("--------------------------- CHECKOUT %d ---------------------------\n", i);
                        printf("|%d|->client %d, %d s\n", status.MPI_SOURCE, results.id, results.sleep);
                        checkouts[i-1].free = FREE;
                        /*
                        Se resetea el cliente que se acaba de devolver a la cola en caso de que viniese de una caja prioritaria
                        (ya que en el reseteo general no se ha podido resetear debido a que sino las cajas serían incosistentes y
                        volverían a meter a un cliente que ya pasó anteriormente)
                        */
                        if(results.sent)
                            reset_client(&clients[results.id%MAX_CLIENTS]);
                        completed++;
                        attending--;
                        if(pending>0){
                            //cajas prioritarias
                            if(checkouts[i-1].priority){
                                prior_index = check_prior(clients, nAttend);
                                MPI_Send(&clients[prior_index%MAX_CLIENTS],sizeof(client),MPI_CHAR,i,0,MPI_COMM_WORLD);
                                clients[prior_index%MAX_CLIENTS].sent = 1;
                                printf("|%d|<-client %d, %d s\n", status.MPI_SOURCE,prior_index%MAX_CLIENTS, clients[prior_index%MAX_CLIENTS].sleep);
                                printf("|prioritary|\n");
                            } else{
                                while(clients[nAttend%MAX_CLIENTS].sent) nAttend++;
                                //Se envia el cliente a la caja
                                MPI_Send(&clients[nAttend%MAX_CLIENTS],sizeof(client),MPI_CHAR,i,0,MPI_COMM_WORLD);
                                printf("|%d|<-client %d, %d s\n", status.MPI_SOURCE,nAttend%MAX_CLIENTS, clients[nAttend%MAX_CLIENTS].sleep);
                                nAttend++;
                            }
                            
                            checkouts[i-1].free = OCCUPIED;
                            pending--;
                            attending++;
                            pending++;
                            //Función no bloqueante que recibe el resultado de la caja
                            MPI_Irecv(&results, sizeof(client),MPI_CHAR, i, 0, MPI_COMM_WORLD, &request[i-1]);
                            
                            //se resetea el cliente menos si se ha enviado a una caja prioritaria
                            if(nAttend>=MAX_CLIENTS && !clients[nAttend%MAX_CLIENTS].sent){
                                reset_client(&clients[nAttend%MAX_CLIENTS]);
                            }
                        }
                        
                        // for(i=0;i<size-1;i++){
                        //     printf("checkout %d active: %c free: %c prioritary: %c\n",
                        //         i+1, checkouts[i].active?'O':'X', checkouts[i].free?'O':'X', checkouts[i].priority?'O':'X');
                        // }
                        //printf("\n\n");
                        //print_clients(clients, nAttend, pending);
                        printf("Completed: %d. Pending: %d. Attending: %d. nAttend: %d. Open: %d.\n",completed, pending, attending, nAttend, nActive);
                        printf("------------------------------------------------------------------\n\n");
                    }
                }      
            }
        } else if(rank>0){  //ESCLAVO
            attend_client();
        }
    }

    //Terminamos la comunicación
    for(i=1;i<size;i++){
        MPI_Recv(&results, 1, MPI_2INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
    MPI_Finalize();

    return 0;
}

void attend_client(){
    struct client message;
    MPI_Recv(&message,sizeof(client),MPI_CHAR,MASTER,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    sleep(message.sleep);
    MPI_Send(&message, sizeof(client),MPI_CHAR,MASTER,0,MPI_COMM_WORLD);
}   

int check_clients_size(struct checkout* checkouts, int nActive, int attending, int pending, int completed, int size){
    int i;
    if(nActive*2 <= pending && nActive < size-1){
        nActive++;
        for(i=0;i<nActive;i++){
            checkouts[i].active = OPEN;
        }
    }
    else if(nActive > pending && nActive > attending){
        nActive--;
        for(i=size-1;i>nActive;i--){
            checkouts[i].active = CLOSED;
        }
    }
    return nActive;
}

void reset_clients(struct client* clients){
    int i;
    for(i=0; i<MAX_CLIENTS;i++){
        reset_client(&clients[i]);
    }
}

void reset_client(struct client* client){
    //(*client).id += MAX_CLIENTS;
    (*client).sent = 0;
    (*client).priority = rand()%2;
    if((*client).priority){
        (*client).sleep=rand()%11+10;
    }  
    else{
        (*client).sleep=rand()%6+5;
    }
        
}

void initialize_clients(struct client* clients){
    int i;
    for(i=0;i<MAX_CLIENTS;i++){
        clients[i].id = i;
        clients[i].sent = 0;
        clients[i].priority = rand() % 2;
        if(clients[i].priority)
            clients[i].sleep=rand()%11+10;
        else
            clients[i].sleep=rand()%6+5;
    }
}

void openclose_priorcheckouts(struct checkout* checkouts, int nActive){
    int i, aux;
    for(i=0;i<nActive;i++){
        checkouts[i].priority = 0;
    }
    aux = nActive/4;
    for(i=0;i<aux;i++){
        checkouts[i].priority = 1;
    }   
}

int check_prior(struct client* clients, int nAttend){
    while(!clients[nAttend%MAX_CLIENTS].priority) nAttend++;
    return nAttend;
}

void order_clients(struct client* clients, int nAttend, int pending){
    int i;
    for(i=nAttend;i<nAttend+pending-1;i++){
        clients[i] = clients[i+1];
    }
}

void print_clients(struct client* clients, int nAttend, int pending){
    int i;
    for(i=nAttend;i<nAttend+pending;i++){
        printf("client %d sleep: %d prioritary: %d\n", i, clients[i%MAX_CLIENTS].sleep, clients[i%MAX_CLIENTS].priority);
    }
}