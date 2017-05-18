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
    char *opt = argv[1];

    /* Open HCI socket  */
    int ctl;
    if ((ctl = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI)) < 0) {
        perror("Can't open HCI socket.");
        exit(1);
    }

    struct hci_dev_req dr;

    int dev_id = hci_get_route(NULL);
    dr.dev_id  = dev_id;
    dr.dev_opt = SCAN_DISABLED;

    if (opt == NULL || !strcmp(opt, "piscan"))
        dr.dev_opt = SCAN_PAGE | SCAN_INQUIRY;
    else if (!strcmp(opt, "iscan"))
        dr.dev_opt = SCAN_INQUIRY;
    else if (!strcmp(opt, "pscan"))
        dr.dev_opt = SCAN_PAGE;
    else if (!strcmp(opt, "off") || !strcmp(opt, "down") || !strcmp(opt, "disable"))
        dr.dev_opt = SCAN_DISABLED;


    printf("%d", dr.dev_opt);

    if (ioctl(ctl, HCISETSCAN, (unsigned long) &dr) < 0) {
        fprintf(stderr, "Can't set scan mode on hci%d: %s (%d)\n",
                        dev_id, strerror(errno), errno);
        exit(1);
    }

    return 0;
}

