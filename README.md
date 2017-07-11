# raspberry pi bluez examples

Some C coding examples of working with the bluez library on the raspberry pi for experience and practice

### Installation

```
$ sudo apt-get update
$ sudo apt-get install python-pip python-dev ipython
$ sudo apt-get install bluetooth libbluetooth-dev
$ sudo pip install pybluez
$ sudo apt-get install python-dbus
```

### Compile

```
$ gcc -o discoverable discoverable.c -lbluetooth
```

### Usage

```
$ ./discoverable piscan
```

### Helpful tools

```
$ dbus-send --system --print-reply --dest=org.bluez /org/bluez/hci0 org.freedesktop.DBus.Introspectable.Introspect
```

### Reference material

http://people.csail.mit.edu/albert/bluez-intro
