A lightweight ipc perf testset using posix message queue or unix domain sockets.
Recv / xmit threads are using FIFO scheduling and you may configure their priority. For the timestamp and math the class TimeProfiling.h is used.

# Prepare 
## Build within
```
make clean &&  make
```

# Measure

## With message queue (MQ)
Start receive
```
sudo -i
./recv/mq-perf-recv --ipc=mq --prio=50
```

Start xmit 
```
sudo -i
./xmit/mq-perf-xmit --ipc=mq --prio=40 --time=6000 --burst=15
```

## With unix domain sockets (UDS)
Start receive
```
sudo -i
./recv/mq-perf-recv --ipc=uds --prio=50
```

Start xmit
```
sudo -i
./xmit/mq-perf-xmit --ipc=uds --prio=40 --time=6000 --burst=15
```

## With shared memory IPC (shmem)
Start receive
```
sudo -i
./recv/mq-perf-recv --ipc=shmem --prio=50
```

Start xmit
```
sudo -i
./xmit/mq-perf-xmit --ipc=shmem --prio=40 --time=6000 --burst=15
```

after a while exit mq-perf-xmit by press q
then exit mq-perf-recv  by press q