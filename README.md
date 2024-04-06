# OTP

A One Time Password management linux kernel module and it's additional tool

First, you will need to install `otp_tool`. To do so, make sure [cargo](https://www.rust-lang.org/tools/install) is installed on your system.

```sh
cargo build --release --manifest-path otp_tool/Cargo.toml
sudo install otp_tool/target/release/otp_tool /usr/local/bin/
```

Then check the installation:

```sh
otp_tool
```

```
OTP tools - manage your OTP devices

Usage: otp_tool <COMMAND>

Commands:
  status       Display devices status
  set-devices  Change the numbers of devices
  set-mode     Change a device mode
  request      Request a one time password from a device
  validate     Validate a one time password on a device
  help         Print this message or the help of the given subcommand(s)

Options:
  -h, --help     Print help
  -V, --version  Print version
```

## Build OTP module

To build the OTP manager linux kernel module, enter the following command:

```sh
make -C otp
```

## Install OTP module

Then, to install the OTP module, you can enter for example the following command:

```sh
insmod otp/otp.ko devices=3 pwd_list=p4ssw0rd,12345,kernel,qwerty
```

`devices`: create X OTP devices

example with parameter `devices=3`:

```sh
ls /dev/ | grep otp
```

```
otp0
otp1
otp2
```

`pwd_list`: password list used by devices when they are in `list` mode

## Uninstall OTP module

```sh
rmmod otp
```

## Request OTP

### Using CLI

```sh
cat /dev/otp0 ; echo
```

```
p4ssw0rd
```

### Using otp_tool

```sh
otp_tool request /dev/otp0
```

```
p4ssw0rd
```

## Validate OTP

### Using CLI

With a wrong otp:

```sh
echo -n "b@d p4ssw0rd" > /dev/otp0
```

```
write error: Invalid argument
```

With the otp

```sh
echo -n "p4ssw0rd" > /dev/otp0
```

A second time with the otp

```sh
echo -n "p4ssw0rd" > /dev/otp0
```

```
write error: Invalid argument
```

### Using otp_tool

```sh
otp_tool validate /dev/otp0 p4ssw0rd
```

```
otp has been approved by device '/dev/otpX'
```

## Display devices status

### Using CLI

```sh
cat /proc/otp
```

### Using otp_tool

```sh
otp_tool status
```

```
DEVICE     MODE     PASSWORD
------     ----     --------
otp0       algo     
otp1       list     nEw
otp2       list     P@ssw0RdS
```

## Display password list

```sh
cat /sys/module/otp/parameters/pwd_list
```

```
p4ssw0rd,12345,kernel,qwerty
```

## Edit password list

```sh
echo -n "nEw,BeTtEr,P@ssw0RdS" > /sys/module/otp/parameters/pwd_list
```

## Edit devices nb

### Using CLI

```sh
echo 5 > /sys/module/otp/parameters/devices
```

### Using otp_tool

```sh
otp_tool set-devices 5
```

```sh
ls /dev/ | grep otp
```

```
otp0
otp1
otp2
otp3
otp4
```

## Edit device mode

Put a device in `list` mode (default):

```sh
otp_tool set-mode /dev/otp0 list
```

```
device '/dev/otp0' has been set to mode 'List'
```

Put a device in `algo` mode:

```sh
otp_tool set-mode /dev/otp0 algo
```

```
device '/dev/otp0' has been set to mode 'Algo'
```
