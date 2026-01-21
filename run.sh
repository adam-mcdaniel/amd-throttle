#!/usr/bin/env bash
#SBATCH -A csc688
#SBATCH -N 1
#SBATCH -p batch
#SBATCH --exclusive
#SBATCH -t 00:02:00
#SBATCH -J gpu-throttling-study
#SBATCH -o slurm-%j.out
#SBATCH -e slurm-%j.err
#SBATCH -C nvme
#SBATCH --gpu-power-cap=300


if [ -f ../../setup-env.sh ]; then
  echo "Setting up ScoreP"
  source ../../setup-env.sh
  source ../setup-run-params.sh
else
  echo "Skipping ScoreP setup"
fi

export ITERATIONS=1
export IDLE_PERIOD_MS=1
export ACTIVE_PERIOD_MS=60000
export VECTOR_SIZE=134217728

module load amd/6.4.1
module load rocm/6.4.1
module load PrgEnv-amd/8.6.0

rm -f gpu_throttling_output.txt

(
  while true; do
    ./gpu_metrics8_throttling >> gpu_throttling_output.txt
    sleep 0.01
  done
) &
WATCH_PID=$!

# Run the GPU application that generates a square wave pattern
srun -n 8 -c 7 --gpus-per-task=1 --gpu-bind=closest ./step_function --vector_size $VECTOR_SIZE --n_steps $ITERATIONS --time_active $ACTIVE_PERIOD_MS --time_sleep $IDLE_PERIOD_MS

# After the application finishes, kill the watch process
kill $WATCH_PID