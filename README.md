# ecpprog

A basic driver for FTDI based JTAG probes (FT232H, FT2232H, FT4232H), to program Lattice ECP5/Nexus FPGAs.

## Features:
 - SPI Flash programing via JTAG link to ECP5/NX part.
 - Validate ECP5/NX IDCODEs
 - Read/Decode ECP5/NX status register

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

## Usage

### Verify JTAG connection
```
$ ecpprog -t
  init..
  IDCODE: 0x41111043 (LFE5U-25)
  ECP5 Status Register: 0x00200000
  flash ID: 0xEF 0x40 0x18 0x00
  Bye.
```

### Flash a bitstream
```
$ ecpprog /path/to/bitstream.bit
  init..
  IDCODE: 0x41111043 (LFE5U-25)
  ECP5 Status Register: 0x00200000
  reset..
  flash ID: 0xEF 0x40 0x18 0x00
  file size: 99302
  erase 64kB sector at 0x000000..
  erase 64kB sector at 0x010000..
  programming..  99302/99302
  verify..       99302/99302  VERIFY OK
  Bye.
```

### Flash User/SoC code
```
$ ecpprog -o 1M firmware.bin 
  init..
  IDCODE: 0x41111043 (LFE5U-25)
  ECP5 Status Register: 0x00200000
  reset..
  flash ID: 0xEF 0x40 0x18 0x00
  file size: 294312
  erase 64kB sector at 0x100000..
  erase 64kB sector at 0x110000..
  erase 64kB sector at 0x120000..
  erase 64kB sector at 0x130000..
  erase 64kB sector at 0x140000..
  programming..  294312/294312
  verify..       294312/294312  VERIFY OK
  Bye.

```