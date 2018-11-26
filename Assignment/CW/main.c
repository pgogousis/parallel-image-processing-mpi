
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include <time.h>

#include "pgmio.h"

#define WIDTH 192
#define HEIGHT 128
#define PROCS 4
#define PWIDTH WIDTH/2
#define PHEIGHT HEIGHT/2
#define TRUE 1
#define FALSE 0
#define MAX_ITERS 1500

void checkNumberOfArgs(char *argument, int world_size);

int main(int argc, char *argv[])
{
  clock_t start, end;
  start = clock();
  double cpu_time_used;

  MPI_Comm comm2d;
  int periodic[2], reorder;
  periodic[0] = TRUE; // Vertically periodic
  periodic[1] = FALSE; // Horizontally not periodic
  reorder = FALSE;
  int coord[2], id;
  int rank_up, rank_down, rank_left, rank_right;

  double master_image[WIDTH][HEIGHT]; // Array for master process to store initial edge image
  double image[PWIDTH][PHEIGHT]; // Array for each process to store edge images locally
  // double edge[PWIDTH + 2][PHEIGHT + 2];
  double old[PWIDTH + 2][PHEIGHT + 2]; // Array for each process used for the calculation
  double new[PWIDTH + 2][PHEIGHT + 2]; // Array for each process used for the calculation
  int width, height; // Image width and height - pixels

  MPI_Init(NULL, NULL); // Initialize MPI

  int world_size; // Get world size
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);

  MPI_Comm comm;
  comm = MPI_COMM_WORLD;

  MPI_Status status;
  MPI_Request request;

  //checkNumberOfArgs(argv[0], world_size); // checking the number of arguments given

  int this_rank; // Get rank id
  MPI_Comm_rank(comm, &this_rank);

  int decomp_params[2]; // Used to store the decomposition parameters
  MPI_Dims_create(world_size, 2, &decomp_params[0]); // Funtion to decide how to split image among processes

  MPI_Cart_create(comm, 2, decomp_params, periodic, reorder, &comm2d);

  MPI_Datatype array_block;
  MPI_Type_vector(WIDTH/decomp_params[0], HEIGHT/decomp_params[1], HEIGHT, MPI_DOUBLE, &array_block);
  MPI_Type_commit(&array_block);

  if(this_rank == 0){
    printf("~~~~~~~~~\nSuggested M split = %d and N split = %d\n", decomp_params[0], decomp_params[1]);
    char *filename;
    filename = "img/edge192x128.pgm";
    pgmsize(filename, &width, &height);
    printf("~~~~~~~~~\n\"%s\" width = %d and height = %d\n", filename, width, height);
    // Import file to image array
    printf("~~~~~~~~~\nReading \"%s\"\n", filename);
    pgmread(filename, master_image, WIDTH, HEIGHT);

    // Send subarrays to processes
    int w_index, h_index;
    int current_process = 1;
    for(w_index = 0; w_index < decomp_params[0]; ++w_index){
      for(h_index = 0; h_index < decomp_params[1]; ++h_index){
        if((w_index == 0) && (h_index == 0)){
          continue;
        }
        // Change parameters!!! - should be generic
        MPI_Issend(&master_image[w_index*PWIDTH][h_index*PHEIGHT], 1, array_block, current_process++, 0, comm2d, &request);
      }
    }
    // Local copy for process 0
    int i, j;
    for(i = 0 ; i < WIDTH/2 ; ++i){
      for(j = 0 ; j < HEIGHT/2 ; ++j){
        image[i][j] = master_image[i][j];
      }
    }
  }
  else {
    // Process receives data from 0 and stores it in its local buffer (image array)
    MPI_Recv(&image[0][0], PWIDTH*PHEIGHT, MPI_DOUBLE, 0, 0, comm2d, &status);
  }

  // Initialize old array with edge image values
  int i, j;
  for(i = 0 ; i < PWIDTH+2 ; ++i){
    for(j = 0 ; j < PHEIGHT+2 ; ++j){
      if((i == 0)||(j == 0)||(i == PWIDTH+1)||(j == PHEIGHT+1)){
        old[i][j] = 255; // White just to see it better in pgms for testing
      }
      else{
        old[i][j] = image[i-1][j-1];
      }
    }
  }

  MPI_Cart_coords(comm2d, this_rank, 2, coord);
  printf("I am %d and my coords are: %d and %d\n", this_rank, coord[0], coord[1]);
  MPI_Cart_shift(comm2d, 0, 1, &rank_up, &rank_down);
  MPI_Cart_shift(comm2d, 1, 1, &rank_left, &rank_right);

  if(rank_left < 0){
    rank_left = MPI_PROC_NULL;
  }
  if(rank_right < 0){
    rank_right = MPI_PROC_NULL;
  }

  printf("Neighbors: left = %d right = %d up = %d down = %d\n", rank_left, rank_right, rank_up, rank_down);

  MPI_Request request_array[4];
  MPI_Irecv(&old[0][0], PHEIGHT, MPI_DOUBLE, rank_down, 0, comm2d, &request_array[0]);
  MPI_Isend(&old[PWIDTH][0], PHEIGHT, MPI_DOUBLE, rank_up, 0, comm2d, &request_array[2]);
  MPI_Irecv(&old[PWIDTH+1][0], PHEIGHT, MPI_DOUBLE, rank_up, 1, comm2d, &request_array[1]);
  MPI_Isend(&old[0][0], PHEIGHT, MPI_DOUBLE, rank_down, 1, comm2d, &request_array[3]);

  MPI_Status status_array[4];
  MPI_Waitall(4, request_array, status_array);



  // foo print - no reason to exist apart from satisfying me :P
  char out[10];
  sprintf(out, "out%d.pgm", this_rank);
  pgmwrite(out, &old[0][0], PWIDTH+2, PHEIGHT+2);

  if(this_rank == 0){
    // Export image to output file
    MPI_Request request_array[PROCS-1];
    MPI_Status status_array[PROCS-1];

    int w_index, h_index;
    int current_process = 1;
    for(w_index = 0; w_index < decomp_params[0]; w_index++){
      for(h_index = 0; h_index < decomp_params[1]; h_index++){
        if((w_index == 0) && (h_index == 0)){
          continue;
        }
        MPI_Recv(&master_image[w_index*PWIDTH][h_index*PHEIGHT], 1, array_block, current_process++, 0, comm2d, &status_array[current_process-1]);
        // MPI_Irecv(&master_image[w_index*PWIDTH][h_index*PHEIGHT], 1, array_block, current_process++, 0, comm2d, &request_array[current_process-1]);
      }
    }
    // MPI_Waitall(PROCS-1, request_array, status_array);

    char *outputfile;
    outputfile = "out.pgm";
    printf("~~~~~~~~~\n");
    pgmwrite(outputfile, &master_image[0][0], WIDTH, HEIGHT);
    end = clock();
    cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("~~~~~~~~~\nFinished %d iterations in %f seconds\n", MAX_ITERS, cpu_time_used);
  }
  else {
    MPI_Issend(&image[0][0], PWIDTH*PHEIGHT, MPI_DOUBLE, 0, 0, comm2d, &request);
  }

  MPI_Finalize();

}

void checkNumberOfArgs(char *argument, int world_size){
    if(world_size != PROCS){
        fprintf(stderr, "World size must be 4 for %s\n", argument);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
}
