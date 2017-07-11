#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

int main(int argc, char **argv)
{
    char *hexstring = argv[1];

    int id;
    int fh;
    bdaddr_t btaddr;
    char pszaddr[18];
 
    // 0x280404 - Headset Profile
    unsigned int cls = 0x000000;
    int timeout = 1000;
 
    printf("this example should be run as root\n");

    if (hexstring != NULL)
	cls = (unsigned int)strtol(hexstring, NULL, 0);
 
    // get the device ID
    if ((id = hci_get_route(NULL)) < 0)
        return -1;
 
    // convert the device ID into a 6 byte bluetooth address
    if (hci_devba(id, &btaddr) < 0)
        return -1;
 
    // convert the address into a zero terminated string
    if (ba2str(&btaddr, pszaddr) < 0)
        return -1;
 
    // open a file handle to the HCI
    if ((fh = hci_open_dev(id)) < 0)
        return -1;
 
    // set the class
    if (hci_write_class_of_dev(fh, cls, timeout) != 0)
    {
        perror("hci_write_class ");
        return -1;
    }
 
    // close the file handle
    hci_close_dev(fh);
 
    printf("set device %s to class: 0x%06x\n", pszaddr, cls);
 
    return 0;
}
