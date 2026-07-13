# Development Log

## Stage 0

Completed kernel module build environment and Git setup.

## Stage 1

Implemented character device framework.

Solved user/kernel structure ABI mismatch.

## Stage 2

Implemented hrtimer sampling and Ring Buffer.

Solved timer initialization and nested spinlock issues.

## Stage 3

Implemented ioctl control:

-   START
-   STOP
-   CLEAR_BUFFER
-   SET_RATE
-   STATUS

Solved blocked reader wakeup problem using wait queue condition and
wake_up_interruptible.

## Stage 4: Event-driven I/O

Implemented:

- driver poll callback
- poll/select compatibility
- non-blocking read
- epoll LT mode
- epoll ET mode
- multiple file descriptor monitoring
- buffered-data draining before HUP

Problems solved:

### Incorrect readable state

- The result of ring_empty() was initially assigned directly to readable,
causing poll to report the opposite state.

- Solution:

```c
readable = !vdaq_ring_empty(dev);
```

- ET mode did not report HUP after draining

After the final buffered sample was read, epoll was not notified that the
device had entered the stopped-and-empty state.

- Solution:

Wake the read queue when the last sample is consumed while the device is
stopped, allowing the poll callback to return EPOLLHUP.
