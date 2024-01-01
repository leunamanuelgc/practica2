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

struct client{
    int process;
    int sleep;
    char priority;
};

struct checkout{
    char active;
    char free;
    char priority;
};

void attend_client();
int check_clients_size(struct checkout* checkouts, int nActive, int attending, int pending, int completed, int size);
void reset_client(struct client _client);
void initialize_clients(struct client* _clients);

int main(int argc, char** argv){
    int rank, /*rango*/ size, /*nº procesos*/ i, /*iterador*/
        attending, /*nº clientes atendiéndose*/
        pending, /*nº clientes pendientes*/
        nAttend, /*nº clientes atendidos*/
        completed, /*nº clientes completados*/
        flag, /*true si MPI_Irecv recibe un mensaje*/
        nActive /*nº cajas activas*/;

    struct client* clients; //clientes
    struct client results;  //variable que guarda el mensaje de la caja que ha enviado el mensaje
    struct checkout* checkouts; //cajas

    MPI_Request* request;   //request para saber si se ha recibido un mensaje
    MPI_Status status;      //variable utilizada para conocer quién envió el mensaje

    clock_t clock;

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
        //Inicializando variables
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
        
        for(i=0;i<size-1;i++){
            printf("checkout %d active: %c free: %c\n", i+1, checkouts[i].active?'O':'X', checkouts[i].free?'O':'X');
        }
        printf("n process=%d, max_clients in queue=%d, master=%d, checkouts=%d, ", size, MAX_CLIENTS, MASTER, (size-1));
        printf("completed: %d, pending: %d, attending: %d, open: %d\n\n\n",completed, pending, attending, nActive);
        
        
        //Mandando mensajes
        for(i=0;i<nActive;i++){
            if(checkouts[i].active){
                nActive=check_clients_size(checkouts, nActive, attending, pending, completed, size);
                //Se asigna al cliente su caja
                clients[i].process = i;
                //Se envia el cliente a la caja
                MPI_Send(&clients[nAttend],1,MPI_2INT,i,0,MPI_COMM_WORLD);
                //Gestión de la cola y las cajas
                printf("Sending client %d to checkout nº %d\n", nAttend+1, i);
                printf("Time remaining for checkout nº %d: %d seconds\n", i, clients[nAttend].sleep);
                checkouts[i].free = OCCUPIED;
                pending--;
                attending++;
                nAttend++;
                printf("%d active: %d, free: %d\n", i, checkouts[i].active, checkouts[i].free);
                printf("Completed: %d. Pending: %d. Attending: %d. Open: %d\n\n",completed, pending, attending, nActive);
                //Función no bloqueante que recibe el resultado de la caja
                MPI_Irecv(&results, 1, MPI_2INT, i, 0, MPI_COMM_WORLD, &request[i]);
            }
            
        }
    
    } else if (rank > 0) {  //ESCLAVO
        attend_client();
    }

    while(1){
        if(rank == 0){  //MAESTRO
            for(i=1;i<size;i++){
            nActive=check_clients_size(checkouts, nActive, attending, pending, completed, size);    
                //comprobamos si la caja está activa y ocupada
                if(checkouts[i-1].active && !checkouts[i-1].free){
                    //comprobamos si la caja ha enviado un mensaje
                    MPI_Test(&request[i-1],&flag,&status);
                    if(flag){
                        printf("Checkout nº %d finished and lasted %d seconds.\n", status.MPI_SOURCE, results.sleep);
                        checkouts[i-1].free = FREE;
                        completed++;
                        attending--;
                        if(pending>0){
                            //Se envia el cliente a la caja
                            MPI_Send(&clients[nAttend%MAX_CLIENTS],1,MPI_2INT,i,0,MPI_COMM_WORLD);
                            //Gestión de la cola y las cajas
                            printf("Sending client %d to checkout nº %d\n", nAttend+1, status.MPI_SOURCE);
                            printf("New time remaining for checkout nº %d: %d seconds\n\n", status.MPI_SOURCE, clients[nAttend].sleep);
                            checkouts[i-1].free = OCCUPIED;
                            pending--;
                            attending++;
                            nAttend++;
                            pending++;
                            reset_client(clients[nAttend%MAX_CLIENTS]);
                            //Función no bloqueante que recibe el resultado de la caja
                            MPI_Irecv(&results, 1, MPI_2INT, i, 0, MPI_COMM_WORLD, &request[i-1]);
                        }
                        
                        for(i=0;i<size-1;i++){
                            printf("checkout %d active: %c free: %c\n", i+1, checkouts[i].active?'O':'X', checkouts[i].free?'O':'X');
                        }
                        printf("Completed: %d. Pending: %d. Attending: %d. nAttend: %d. Open: %d.\n\n\n",completed, pending, attending, nAttend, nActive);
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
    MPI_Recv(&message,1,MPI_2INT,MASTER,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    sleep(message.sleep);
    MPI_Send(&message, 1, MPI_2INT,MASTER,0,MPI_COMM_WORLD);
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

void reset_client(struct client _client){
    _client.priority = rand()%2;
    _client.sleep?rand()%11+10:rand()%6+5;
}

void initialize_clients(struct client* _clients){
    int i;
    for(i=0;i<MAX_CLIENTS;i++){
        _clients[i].priority = rand() % 2;
        _clients[i].sleep?rand()%11+10:rand()%6+5;
    }
}
