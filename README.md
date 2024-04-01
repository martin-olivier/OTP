# OTP

A One Time Password management linux kernel module and it's additional tool

First, you will need to install `otp_tool`. To do so, make sure [cargo](https://www.rust-lang.org/tools/install) is installed on your system.

```sh
cargo install --path otp_tool
```

Then check the installation:

```
$ otp_tool
OTP tools - manage your OTP devices

Usage: otp_tool <DEVICE> <COMMAND>

Commands:
  set-mode  Change the device mode
  request   Request a one time password
  validate  Validate a one time password
  help      Print this message or the help of the given subcommand(s)

Arguments:
  <DEVICE>  OTP device path

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

```
$ ls /dev/ | grep otp
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

```
$ cat /dev/otpX ; echo
p4ssw0rd
```

### Using otp_tools

```
$ otp_tool /dev/otpX request
p4ssw0rd
```

## Validate OTP

### Using CLI

With a wrong otp:

```
$ echo -n "b@d p4ssw0rd" > /dev/otp
write error: Invalid argument
```

With the otp

```
$ echo -n "p4ssw0rd" > /dev/otp
```

A second time with the otp

```
$ echo -n "p4ssw0rd" > /dev/otp
write error: Invalid argument
```

### Using otp_tools

```
$ otp_tool /dev/otpX validate p4ssw0rd
otp has been approved by device '/dev/otpX'
```

## Display password list

```
$ cat /sys/module/otp/parameters/pwd_list
p4ssw0rd,12345,kernel,qwerty
```

## Edit password list

```
$ echo "nEw,BeTtEr,P@ssw0RdS" > /sys/module/otp/parameters/pwd_list
```

## Edit devices nb

```
echo 5 > /sys/module/otp/parameters/devices
```

```
$ ls /dev/ | grep otp
otp0
otp1
otp2
otp3
otp4
```

## Edit device mode

Put a device in `list` mode (default):

```
$ otp_tool /dev/otpX set-mode list
device '/dev/otpX' has been set to mode 'List'
```

Put a device in `algo` mode:

```
$ otp_tool /dev/otpX set-mode algo
device '/dev/otpX' has been set to mode 'Algo'
```
