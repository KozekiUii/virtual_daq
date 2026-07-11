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

## Next

-   poll/select/epoll
-   fault recovery
-   sysfs interface
