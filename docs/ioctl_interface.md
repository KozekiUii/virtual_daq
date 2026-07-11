# IOCTL Interface

## Overview

User applications control the driver through ioctl.

## Commands

  Command        Description
  -------------- ----------------------
  GET_STATUS     Get runtime status
  START          Start acquisition
  STOP           Stop acquisition
  SET_RATE       Change sampling rate
  CLEAR_BUFFER   Clear FIFO

## SET_RATE

Example:

    vdaq_ctl rate 200
