# Virtual DAQ System Architecture

## Overview

Linux kernel based virtual multi-channel data acquisition driver.

## Architecture

    User Space
     reader / vdaq_ctl
            |
         read/ioctl
            |
    Kernel Space
     vdaq character driver
            |
     +----------------+
     | Control Path   |
     | mutex          |
     +----------------+
            |
     +----------------+
     | Data Path      |
     | hrtimer        |
     | Ring Buffer    |
     | spinlock       |
     +----------------+

## Data Flow

    hrtimer callback -> generate sample -> Ring Buffer -> read() -> User

## Synchronization

Data path uses spinlock. Control path uses mutex.
