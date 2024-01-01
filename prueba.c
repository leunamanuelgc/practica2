#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char** argv){
    int rank, np;
    int message;
    int buf;
    int* result;
    int count;
    MPI_Request* request;
    MPI_Status* status;
    int* flag;
    int* finished;
    int i;

    MPI_Init(NULL,NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &np);

    if(rank==0){
        result = (int*)malloc(sizeof(int)*(np-1));
        request = (MPI_Request*)malloc(sizeof(MPI_Request)*(np-1));
        status = (MPI_Status*)malloc(sizeof(MPI_Status)*(np-1));
        flag = (int*)malloc(sizeof(int)*(np-1));
        finished = (int*)malloc(sizeof(int)*(np-1));
        for(i=0;i<np-1;i++){
            finished[i] = 0;
        }
        count = 0;
        for(i=1;i<np;i++){
            message = i;
            MPI_Send(&message, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
            printf("proceso %d duerme %d segundos\n", i, message);
            MPI_Irecv(&result[i-1], 1, MPI_INT, i, 0, MPI_COMM_WORLD, &request[i-1]);
        }
    } else {
        MPI_Recv(&buf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        sleep(buf);
        MPI_Send(&buf, 1, MPI_INT, 0, 0, MPI_COMM_WORLD);
    }

    if(rank == 0){
        while(count < np-1){
            for(int i=1;i<np;i++){
                if(!finished[i-1]){
                    MPI_Test(&request[i-1], &flag[i-1], &status[i-1]);
                    if(flag[i-1] == 1){
                        printf("Recibido %d segundos del proceso %d\n",result[i-1], status[i-1].MPI_SOURCE);
                        count++;
                        finished[i-1] = 1;
                    }
                }   
            }
        }   
    }
    
    MPI_Finalize();
    return 0;
}