# ecpprog

A basic driver for FTDI based JTAG probes, to program ECP5 FPGAs.

## Features:
 - Flash programing via JTAG link to ECP5 part.
 - Validate ECP5 IDCODEs
 - Read/Decode ECP5 status register

## Prerequisites

```
sudo apt-get install libftdi-dev
```

## Building

```
git clone https://github.com/gregdavill/ecpprog ecpprog
cd ecpprog/ecpprog
make
sudo make install
```