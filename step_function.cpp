#include <iostream>
#include <mpi.h>
#include <hip/hip_runtime.h>

#ifndef N_ITER
#endif

char* get_parameter(const std::string & option, char ** begin, char ** end){
  char ** itr = std::find(begin, end, option);
  if (itr != end && ++itr != end)
  {
    return *itr;
  }
  return 0;
}

bool parameter_exists(const std::string& option, char** begin, char** end ){
  return std::find(begin, end, option) != end;
}

template<typename T, int iter>
__global__ void vectorAdd(T *buf, const uint64_t n) {
    const uint32_t gid = hipBlockDim_x * hipBlockIdx_x + hipThreadIdx_x;
    const uint32_t nThreads  = gridDim.x * blockDim.x;
    const int nEntriesPerThread = n / nThreads;
    const uint64_t maxOffset = nEntriesPerThread * nThreads;

    T *ptr;
    const T y = (T) 1.0;

    ptr = &buf[gid];
    T x = (T) 2.0;

    // For every vector element, its doing one read
    // For every vector element, its doing 2 * iter flops
    // For every thread, its doing one write
    
    for (uint64_t offset = 0; offset < maxOffset; offset += nThreads) {
        for (int j = 0; j < iter; j++) {
            x = ptr[offset] * x + y;
        }
    }
    ptr[0] = -x;
}



int main(int argc, char** argv) {
  
  int time_sleep = 5000; //milliseconds
  int time_active = 5000; //milliseconds
  int n_steps = 5;
  uint64_t n = 1024*1024*1024;
  uint64_t n_experiments = 100;

  if (parameter_exists("--vector_size", argv, argv+argc)) n = std::stoi(get_parameter("--vector_size", argv, argv+argc));
  if (parameter_exists("--time_sleep", argv, argv+argc)) time_sleep = std::stoi(get_parameter("--time_sleep", argv, argv+argc));
  if (parameter_exists("--time_active", argv, argv+argc)) time_active = std::stoi(get_parameter("--time_active", argv, argv+argc));
  if (parameter_exists("--n_steps", argv, argv+argc)) n_steps = std::stoi(get_parameter("--n_steps", argv, argv+argc));
  
  // time_sleep = static_cast<int>(time_sleep / 1e3);
  
  int rank, size;
  MPI_Init(&argc, &argv);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  
  if (n <134217728){
   if (rank ==0) std::cout << "WARNING: vector_size is too small. setting to 134217728" << std::endl;
  }
  
  if ( rank == 0){
    std::cout << "Vector length: " << n << std::endl;
    std::cout << "N steps: " << n_steps << std::endl;
    std::cout << "Time active [millisecs]: " << time_active << std::endl;
    std::cout << "Time sleep [millisecs]: " << time_sleep << std::endl;
  }
  
  int n_devices = 0;
  hipGetDeviceCount(&n_devices);
  
  if ( n_devices > 1){
    if ( rank >= n_devices ){
      std::cout << "WARNING: Setting more than one rank per device. " << std::endl;
    }
    hipSetDevice(rank);
  } else {
    hipSetDevice(0);
  }
  
  int device_id;
  hipGetDevice(&device_id);
  std::cout << "Process " << rank << " device: " << device_id << "/" << n_devices << std::endl;

  double *dev_mem_a;
  hipMalloc((void**)&dev_mem_a, n * sizeof(double));

  int factor = n / 134217728;
  int blockSize = 256;
  int gridSize = 228 * 128 * factor;
  int numThreads = gridSize * blockSize;
  uint64_t flops = n * N_ITER * 2;
  uint64_t data_moved =  (n + numThreads)*sizeof(double);
  
  if (rank == 0){
    std::cout << "Number of iterations: " << N_ITER << std::endl;
    std::cout << "Grid size: " << gridSize << std::endl;
    std::cout << "Block size: " << blockSize << std::endl;
    std::cout << "Number of threads: " << gridSize * blockSize << std::endl;
    std::cout << "Number of elements per thread: " << n / (gridSize * blockSize) << std::endl;
    std::cout << "Expected number of FP64 Flops: " << flops << std::endl;
    std::cout << "Expected data movement [bytes]: " << data_moved << std::endl;
    std::cout << "Arithmetic Intensity: " << static_cast<float>(flops) / data_moved << std::endl << std::endl;
  }
  
  int n_warmup = 100;
  if (rank == 0) std::cout << "Running warmup: " << n_warmup << " iterations" << std::endl;
  vectorAdd<double, N_ITER><<<gridSize, blockSize>>>( dev_mem_a, n);
  hipEvent_t start, stop;
  hipEventCreate(&start);
  hipEventCreate(&stop);
  float runtime = 0;
  hipEventRecord(start);
  for ( int i=0; i<n_warmup-1; i++){
    vectorAdd<double, N_ITER><<<gridSize, blockSize>>>( dev_mem_a, n);  
  }
  hipEventRecord(stop, 0);
  hipEventSynchronize(stop);
  hipEventElapsedTime(&runtime, start, stop);
  float average_kernel_time = runtime/(n_warmup-1);
  int n_kernel_launch = static_cast<float>(time_active) / (average_kernel_time);

  if (size > 1){
    int32_t n_kernel_launch_global = static_cast<int32_t>(n_kernel_launch);
    MPI_Bcast(&n_kernel_launch_global, 1, MPI_INT32_T, 0, MPI_COMM_WORLD);
    n_kernel_launch = static_cast<int>(n_kernel_launch_global);
  }

  std::cout << "Initial average kernel runtime [ms]: " << average_kernel_time << std::endl;
  std::cout << "Number of kernel launches per step: " << n_kernel_launch << std::endl;
  
  for (int step_index=0; step_index<n_steps; step_index ++){
    
    if (rank == 0) std::cout << "\nStarting step: " << step_index << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(time_sleep));
    hipDeviceSynchronize();
    MPI_Barrier(MPI_COMM_WORLD);
    
    // if (rank == 0)std::cout << "Running experiment: " << n_experiments << " iterations" << std::endl;
    
    hipEventRecord(start);
    for (int i=0; i<n_kernel_launch; i++){
      vectorAdd<double, N_ITER><<<gridSize, blockSize>>>( dev_mem_a, n);
    }
    hipEventRecord(stop, 0);
    hipEventSynchronize(stop);
    hipEventElapsedTime(&runtime, start, stop);
    hipDeviceSynchronize();  
    
    float avg_runtime = runtime / n_kernel_launch;
    double tflops = static_cast<double>(flops) / avg_runtime / 1e9;
    double bw = data_moved / avg_runtime / 1e6;
    std::cout << "rank: " << rank << "  avrg_time [ms]: " << avg_runtime 
      << "  TFLOPS/s: " << tflops << "  BW [GB/s]: " << bw << std::endl;
    
    MPI_Barrier(MPI_COMM_WORLD);
  }
  
  // if (rank == 0) std::cout << "Waiting for " << time_sleep << " seconds" << std::endl;
  std::this_thread::sleep_for(std::chrono::milliseconds(time_sleep));
  if (rank == 0) std::cout << "\nFinished runs" << std::endl << std::endl;
  
  hipFree(dev_mem_a);
  MPI_Finalize();
  return 0;
}

