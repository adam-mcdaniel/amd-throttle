#!/usr/bin/env bash
#SBATCH -N 1
#SBATCH -t 00:02:00
#SBATCH -J gpu-throttling-study
#SBATCH -o slurm-%j.out
#SBATCH -e slurm-%j.err
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

if [ -z "$WAIT_BETWEEN_METRICS_MS" ]; then
  export WAIT_BETWEEN_METRICS_MS=10
  echo "Using default WAIT_BETWEEN_METRICS_MS=$WAIT_BETWEEN_METRICS_MS ms"
else
  echo "Using provided WAIT_BETWEEN_METRICS_MS=$WAIT_BETWEEN_METRICS_MS ms"
fi

source ./load-amd-env.sh

rm -f gpu_throttling_output.txt

(
  while true; do
    ./gpu_metrics8_throttling >> gpu_throttling_output.txt
    sleep $(echo "scale=3; $WAIT_BETWEEN_METRICS_MS / 1000" | bc)
  done
) &
WATCH_PID=$!

# Run the GPU application that generates a square wave pattern
srun -n 8 -c 7 --gpus-per-task=1 --gpu-bind=closest ./step_function --vector_size $VECTOR_SIZE --n_steps $ITERATIONS --time_active $ACTIVE_PERIOD_MS --time_sleep $IDLE_PERIOD_MS

# After the application finishes, kill the watch process
kill $WATCH_PID