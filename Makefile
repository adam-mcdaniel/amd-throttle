N_ITER?=64
CC := gcc
CFLAGS := -Wall -Wextra -O2
HIPCC := hipcc
HIP_MPI_FLAGS := -O3 -DN_ITER=$(N_ITER) -I$(MPICH_DIR)/include -L$(MPICH_DIR)/lib -lmpi 
GPU_ARCH?=gfx90a 
HIP_MPI_FLAGS += --offload-arch=${GPU_ARCH}

.PHONY: all clean run

all: gpu_metrics8_throttling step_function

gpu_metrics8_throttling: gpu_metrics8_throttling.c
	$(CC) $(CFLAGS) gpu_metrics8_throttling.c -o gpu_metrics8_throttling

step_function: step_function.cpp
	$(HIPCC) $(HIP_MPI_FLAGS) step_function.cpp -o step_function	

clean:
	rm -f gpu_metrics8_throttling step_function