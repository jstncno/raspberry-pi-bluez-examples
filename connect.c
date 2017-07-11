#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dbus/dbus.h>

int main(int argc, char **argv) {
    char* btaddr = argv[1];
    if (btaddr == NULL) btaddr = "AC_37_43_D6_02_BD";

    char* path_without_btaddr = "/org/bluez/hci0/dev_";

    char* path;
    path = malloc(strlen(path_without_btaddr)+1+strlen(btaddr));
    strcpy(path, path_without_btaddr);
    strcat(path, btaddr);

    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusError err;

    //init error message
    dbus_error_init (&err);
    const char *name;

    printf("making connection...\n");
    conn = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
    printf("connected: %s\n", dbus_connection_get_is_connected(conn) ? "true" : "false");

    printf("creating new Connect message...\n");
    msg = dbus_message_new_method_call(
        "org.bluez",
        path,
        //"/org/bluez/hci0/dev_AC_37_43_D6_02_BD",
        "org.bluez.Device1", "Connect");
    printf("sending Connect message...\n");

    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);

    if(reply == NULL) printf("replied message is NULL\nError: %s", err.message);

    dbus_message_unref(msg);
    dbus_message_unref(reply);

    dbus_connection_close(conn);
    return 0;
}
