#!/bin/sh

if [ ! -d /tmp/lttng ]; then
  mkdir /tmp/lttng
fi

# relayd gave too mach latency due to network traffic
lttng create -o /tmp/lttng/mq-latency-$(date +%s)
#lttng create mq-latency --set-url=net://192.168.0.139

# enable most important kernel trace points
lttng enable-channel kernel -k
kernel_events=(
  "sched_switch,sched_process_*" "lttng_statedump_*"
  "irq_*" "signal_*" "workqueue_*"
  "kmem_"{mm_page,cache}__{alloc,free} "block_rq_"{issue,complete,requeue}
)

for event in "${kernel_events[@]}"; do
  lttng enable-event -c kernel -k "$event"
done

lttng enable-event -c kernel -k --syscall -a

#lttng track --kernel --pid=`pidof mq-perf-recv`,`pidof mq-perf-xmit`

# enable all user space tracepoints
#lttng enable-channel ust -u
#lttng enable-event -c ust -u -a

# actually start tracing
lttng start
