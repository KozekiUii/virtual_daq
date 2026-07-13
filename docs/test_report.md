# Test Report

## Environment

-   Kernel: 5.10.160
-   Architecture: ARM64
-   Board: LubanCat-5 V2

## Verified

-   Character device operation PASS
-   100Hz sampling PASS
-   Dynamic rate change PASS
-   Ring buffer overflow statistics PASS
-   STOP wakeup blocked reader PASS


## Poll Test

- data available returns POLLIN
- no data causes timeout
- stopped and empty returns POLLHUP

Result: PASS

## Non-blocking Read Test

At 1Hz sampling rate:

- blocking reader waited approximately 1 second
- non-blocking reader returned EAGAIN immediately

Result: PASS

## Epoll LT Test

- continuously reported readable state while buffered data remained
- drained data before reporting HUP

Result: PASS

## Epoll ET Test

- used O_NONBLOCK
- drained data until EAGAIN
- reported HUP after the final buffered sample

Result: PASS