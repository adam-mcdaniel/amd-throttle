# amd-throttle

Collect and identify AMD GPUs throttling events using ROCm's GPU metrics.

## Files

|File|Description|
|-|-|
|[`build.sh`](./build.sh)|Build `gpu_metrics8_throttling.c` and `step_function.cpp`.|
|[`gpu_metrics8_throttling.c`](./gpu_metrics8_throttling.c)| Collects information from the GPU metrics structure in the ROCm driver.|
|[`identify-throttling.sh`](./identify-throttling.sh)|After a run has finished, use this to identify any instances of throttling in the GPU metrics.|
|[`load-amd-env.sh`](./load-amd-env.sh)|Sets up the AMD programming environment when sourced by the other scripts. Change this to change the driver / HIP compiler+runtime used.|
|[`Makefile`](./Makefile)|Used by `./build.sh` under the `load-amd-env.sh` environment.|
|[`run.sh`](./run.sh)|Runs a workload to throttle the GPUs while collecting the metrics in the background.|
|[`step_function.c`](./step_function.cpp)|Run the GPUs at full bore for a period of active/idle time.|

## Build

On MI250X systems, run:

```bash
$ GPU_ARCH=gfx90a ./build.sh
```

On MI300A systems, run:

```bash
$ GPU_ARCH=gfx942 ./build.sh
```

## Run

Once you've finished the build, run the following on your cluster:
```bash
$ sbatch run.sh
```

After the run has finished, use the [`identify-throttling.sh`](./identify-throttling.sh) script to see if any throttling registers status' changed throughout the run.

### Changing the Metrics Collection Interval *(Default 10ms)*
---

Run the script with the `WAIT_BETWEEN_METRICS_MS` variable set to the desired number of milliseconds between collections.

```bash
$ export WAIT_BETWEEN_METRICS_MS=5
$ sbatch run.sh
```

### Changing the Power-Cap *(Optional)*
---

The power cap is set at 300W in the SLURM script at [`run.sh`](./run.sh). Change or remove the `#SBATCH --gpu-power-cap=300` if you desire a different power cap or none at all.

### Setting up with ScoreP *(Optional)*
---

```bash
$ git clone https://github.com/adam-mcdaniel/scorep-amd
$ cd scorep-amd/runs
$ git clone https://github.com/adam-mcdaniel/amd-throttle
```

Follow the instructions in the [ScoreP AMD repo](https://github.com/adam-mcdaniel/scorep-amd) and then build this project:
```bash
$ HIPCC=scorep-hipcc ./build.sh
```