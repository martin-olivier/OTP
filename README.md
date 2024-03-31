# OTP

One Time Password kernel module

## Build

```sh
make
```

## Install

```sh
insmod otp.ko list=p4ssw0rd,12345,kernel,qwerty
```

## Uninstall

```sh
rmmod otp
```

## Request OTP

```sh
cat /dev/otp ; echo
```

```
p4ssw0rd
```

## Validate OTP

With a wrong otp:

```sh
echo -n "b@d p4ssw0rd" > /dev/otp
write error: Invalid argument
```

With the otp

```sh
echo -n "p4ssw0rd" > /dev/otp
```

A second time with the otp

```sh
echo -n "p4ssw0rd" > /dev/otp
write error: Invalid argument
```

## Display password list

```sh
cat /sys/module/otp/parameters/list
```

```
p4ssw0rd,12345,kernel,qwerty
```

## Edit password list

```sh
echo "nEw,BeTtEr,P@ssw0RdS" > /sys/module/otp/parameters/list
```
