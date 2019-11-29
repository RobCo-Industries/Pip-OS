
# Pip OS ![arch](https://img.shields.io/badge/Arch-armv6%20%7C%20armv7--A%20%7C%20armv8--A-blue) ![ver](https://img.shields.io/badge/Version-0.0.1.0-green)

This is the repository for the Operating System used in Pip-Boy products. 
It contains the kernel, an humble implementation of libc, and a tiny user-space environment.


## ROM

A ROM is a packaged version of the OS with pre-configured user applications.
You will need a ROM for a fully functional OS (in order to get a terminal access at startup for example).
ROM developers are responsible of including binaries of **this** project next to their package
when they release their product to the public.


#### Deitrix
The official ROM developed by Robco Industries is Deitrix, and can be found in our repositories.
It's a separated repository to permit people to develop custom environments by themselves.
For more informations about Deitrix, please refer to its repository : 
[https://github.com/RobCo-Industries/Deitrix](https://github.com/RobCo-Industries/Deitrix)


## Supported hardware

The kernel is compatible with the following Broadcom boards, as they share the same underlying architecture :

- BCM2835
- BCM2836
- BCM2837

The model BCM2711 should be supported in the future.

## Building from sources

### Requirements 

- An ARM toolchain compatible to your machine ([GNU ARM](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads))
- An emulator to test the OS (we use [QEMU](https://www.qemu.org/download/))

```sh
cd build 
export BCM=2837 # Build for Raspberry Pi 3 board

make clean
make
make run # Requires QEMU installed on your machine
```

## References

* Peripheral manual for BCM2835 ARM chip (useful to get Memory Mapped IO addresses
[BCM2835 ARM Peripherals](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf)

* Circle - A C++ bare metal environment for Raspberry Pi by rsta2
[https://github.com/rsta2/circle](https://github.com/rsta2/circle)

* Raspberry Pi 3 tutorial by bztsrc
[https://github.com/bztsrc/raspi3-tutorial](https://github.com/bztsrc/raspi3-tutorial)

* Raspberry Pi test environment by mrvn
[https://github.com/mrvn/test](https://github.com/mrvn/test)

* Bare metal Raspberry Pi 2 by Rohansi
[https://github.com/Rohansi/RaspberryPi](https://github.com/Rohansi/RaspberryPi)

* Building an OS for Raspberry Pi by jsandler18
[https://jsandler18.github.io/](https://jsandler18.github.io/)

* OSDev's Raspberry Pi Bare Bones
[https://wiki.osdev.org/Raspberry_Pi_Bare_Bones](https://wiki.osdev.org/Raspberry_Pi_Bare_Bones)

* Learning OS development using Linux kernel and Raspberry Pi by s-matyukevich
[https://github.com/s-matyukevich/raspberry-pi-os](https://github.com/s-matyukevich/raspberry-pi-os)