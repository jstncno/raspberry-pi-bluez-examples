#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <signal.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <glib.h>
#include <dbus/dbus.h>
#include <bluez/gdbus/gdbus.h>
#include <bluez/monitor/uuid.h>
#include <bluez/client/agent.h>
#include <bluez/client/display.h>

#define METHOD_CALL_TIMEOUT (300 * 1000)
#define DBUS_INTERFACE_OBJECT_MANAGER "org.freedesktop.DBus.ObjectManager"

// bluez/gdbus/object.c
static int global_flags = 0;
static gboolean check_experimental(int flags, int flag)
{
    if (!(flags & flag))
        return FALSE;

    return !(global_flags & G_DBUS_FLAG_ENABLE_EXPERIMENTAL);
}

typedef void (* GDBusClientFunction) (GDBusClient *client, void *user_data);

struct generic_data {
    unsigned int refcount;
    DBusConnection *conn;
    char *path;
    GSList *interfaces;
    GSList *objects;
    GSList *added;
    GSList *removed;
    guint process_id;
    gboolean pending_prop;
    char *introspect;
    struct generic_data *parent;
};

struct interface_data {
    char *name;
    const GDBusMethodTable *methods;
    const GDBusSignalTable *signals;
    const GDBusPropertyTable *properties;
    GSList *pending_prop;
    void *user_data;
    GDBusDestroyFunction destroy;
};

static GList *dev_list;
static struct generic_data *root;
static GSList *pending = NULL;

// bluez/gdbus/object.c
static gboolean process_changes(gpointer user_data);
static void process_properties_from_interface(struct generic_data *data,
                        struct interface_data *iface);

struct GDBusClient {
    int ref_count;
    DBusConnection *dbus_conn;
    char *service_name;
    char *base_path;
    guint watch;
    guint added_watch;
    guint removed_watch;
    GPtrArray *match_rules;
    DBusPendingCall *pending_call;
    DBusPendingCall *get_objects_call;
    GDBusWatchFunction connect_func;
    void *connect_data;
    GDBusWatchFunction disconn_func;
    gboolean connected;
    void *disconn_data;
    GDBusMessageFunction signal_func;
    void *signal_data;
    GDBusProxyFunction proxy_added;
    GDBusProxyFunction proxy_removed;
    GDBusClientFunction ready;
    void *ready_data;
    GDBusPropertyFunction property_changed;
    void *user_data;
    GList *proxy_list;
};

struct GDBusProxy {
    int ref_count;
    GDBusClient *client;
    char *obj_path;
    char *interface;
    GHashTable *prop_list;
    guint watch;
    GDBusPropertyFunction prop_func;
    void *prop_data;
    GDBusProxyFunction removed_func;
    void *removed_data;
};

struct method_call_data {
    GDBusReturnFunction function;
    void *user_data;
    GDBusDestroyFunction destroy;
};

// bluez/gdbus/client.c
struct prop_entry {
    char *name;
    int type;
    DBusMessage *msg;
};

// bluez/gdbus/object.c
static void process_property_changes(struct generic_data *data)
{
    GSList *l;

    for (l = data->interfaces; l != NULL; l = l->next) {
        struct interface_data *iface = l->data;

        process_properties_from_interface(data, iface);
    }
}

// bluez/gdbus/object.c
static void append_property(struct interface_data *iface,
            const GDBusPropertyTable *p, DBusMessageIter *dict)
{
    DBusMessageIter entry, value;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL,
                                &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &p->name);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, p->type,
                                &value);

    p->get(p, &value, iface->user_data);

    dbus_message_iter_close_container(&entry, &value);
    dbus_message_iter_close_container(dict, &entry);
}

// bluez/gdbus/object.c
static void append_properties(struct interface_data *data,
                            DBusMessageIter *iter)
{
    DBusMessageIter dict;
    const GDBusPropertyTable *p;

    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
                DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                DBUS_TYPE_STRING_AS_STRING
                DBUS_TYPE_VARIANT_AS_STRING
                DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

    for (p = data->properties; p && p->name; p++) {
        if (check_experimental(p->flags,
                    G_DBUS_PROPERTY_FLAG_EXPERIMENTAL))
            continue;

        if (p->get == NULL)
            continue;

        if (p->exists != NULL && !p->exists(p, data->user_data))
            continue;

        append_property(data, p, &dict);
    }

    dbus_message_iter_close_container(iter, &dict);
}

// bluez/gdbus/object.c
static void append_interface(gpointer data, gpointer user_data)
{
    struct interface_data *iface = data;
    DBusMessageIter *array = user_data;
    DBusMessageIter entry;

    dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, NULL,
                                &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &iface->name);
    append_properties(data, &entry);
    dbus_message_iter_close_container(array, &entry);
}

// bluez/gdbus/object.c
static void append_name(gpointer data, gpointer user_data)
{
    char *name = data;
    DBusMessageIter *iter = user_data;

    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &name);
}

// bluez/gdbus/object.c
static void emit_interfaces_added(struct generic_data *data)
{
    DBusMessage *signal;
    DBusMessageIter iter, array;

    if (root == NULL || data == root)
        return;

    signal = dbus_message_new_signal(root->path,
                    DBUS_INTERFACE_OBJECT_MANAGER,
                    "InterfacesAdded");
    if (signal == NULL)
        return;

    dbus_message_iter_init_append(signal, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                                &data->path);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                DBUS_TYPE_STRING_AS_STRING
                DBUS_TYPE_ARRAY_AS_STRING
                DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
                DBUS_TYPE_STRING_AS_STRING
                DBUS_TYPE_VARIANT_AS_STRING
                DBUS_DICT_ENTRY_END_CHAR_AS_STRING
                DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &array);

    g_slist_foreach(data->added, append_interface, &array);
    g_slist_free(data->added);
    data->added = NULL;

    dbus_message_iter_close_container(&iter, &array);

    /* Use dbus_connection_send to avoid recursive calls to g_dbus_flush */
    dbus_connection_send(data->conn, signal, NULL);
    dbus_message_unref(signal);
}

// bluez/gdbus/object.c
static void remove_pending(struct generic_data *data)
{
    if (data->process_id > 0) {
        g_source_remove(data->process_id);
        data->process_id = 0;
    }

    pending = g_slist_remove(pending, data);
}

// bluez/gdbus/object.c
static void emit_interfaces_removed(struct generic_data *data)
{
    DBusMessage *signal;
    DBusMessageIter iter, array;

    if (root == NULL || data == root)
        return;

    signal = dbus_message_new_signal(root->path,
                    DBUS_INTERFACE_OBJECT_MANAGER,
                    "InterfacesRemoved");
    if (signal == NULL)
        return;

    dbus_message_iter_init_append(signal, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
                                &data->path);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                    DBUS_TYPE_STRING_AS_STRING, &array);

    g_slist_foreach(data->removed, append_name, &array);
    g_slist_free_full(data->removed, g_free);
    data->removed = NULL;

    dbus_message_iter_close_container(&iter, &array);

    /* Use dbus_connection_send to avoid recursive calls to g_dbus_flush */
    dbus_connection_send(data->conn, signal, NULL);
    dbus_message_unref(signal);
}


// bluez/gdbus/object.c
static gboolean process_changes(gpointer user_data)
{
    struct generic_data *data = user_data;

    remove_pending(data);

    if (data->added != NULL)
        emit_interfaces_added(data);

    /* Flush pending properties */
    if (data->pending_prop == TRUE)
        process_property_changes(data);

    if (data->removed != NULL)
        emit_interfaces_removed(data);

    data->process_id = 0;

    return FALSE;
}

static void method_call_reply(DBusPendingCall *call, void *user_data)
{
    struct method_call_data *data = user_data;
    DBusMessage *reply = dbus_pending_call_steal_reply(call);

    if (data->function)
        data->function(reply, data->user_data);

    if (data->destroy)
        data->destroy(data->user_data);

    dbus_message_unref(reply);
}

static void connect_reply(DBusMessage *message, void *user_data)
{
    DBusError error;

    dbus_error_init(&error);

    if (dbus_set_error_from_message(&error, message) == TRUE) {
        printf("Failed to connect: %s\n", error.name);
        dbus_error_free(&error);
        return;
    }

    printf("Connection successful\n");
}

gboolean g_dbus_proxy_method_call(GDBusProxy *proxy, const char *method,
                GDBusSetupFunction setup,
                GDBusReturnFunction function, void *user_data,
                GDBusDestroyFunction destroy)
{
    struct method_call_data *data;
    GDBusClient *client;
    DBusMessage *msg;
    DBusPendingCall *call;

    if (proxy == NULL || method == NULL)
        return FALSE;

    client = proxy->client;
    if (client == NULL)
        return FALSE;

    data = g_try_new0(struct method_call_data, 1);
    if (data == NULL)
        return FALSE;

    data->function = function;
    data->user_data = user_data;
    data->destroy = destroy;

    msg = dbus_message_new_method_call(client->service_name,
                proxy->obj_path, proxy->interface, method);
    if (msg == NULL) {
        g_free(data);
        return FALSE;
    }

    if (setup) {
        DBusMessageIter iter;

        dbus_message_iter_init_append(msg, &iter);
        setup(&iter, data->user_data);
    }

    if (g_dbus_send_message_with_reply(client->dbus_conn, msg,
                    &call, METHOD_CALL_TIMEOUT) == FALSE) {
        dbus_message_unref(msg);
        g_free(data);
        return FALSE;
    }

    dbus_pending_call_set_notify(call, method_call_reply, data, g_free);
    dbus_pending_call_unref(call);

    dbus_message_unref(msg);

    return TRUE;
}

static GDBusProxy *find_proxy_by_address(GList *source, const char *address)
{
    GList *list;

    for (list = g_list_first(source); list; list = g_list_next(list)) {
        GDBusProxy *proxy = list->data;
        DBusMessageIter iter;
        const char *str;

        if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
            continue;

        dbus_message_iter_get_basic(&iter, &str);

        if (!strcmp(str, address))
            return proxy;
    }

    return NULL;
}


// bluez/gdbus/client.c
gboolean g_dbus_proxy_get_property(GDBusProxy *proxy, const char *name,
                                                        DBusMessageIter *iter)
{
    struct prop_entry *prop;

    if (proxy == NULL || name == NULL)
        return FALSE;

    prop = g_hash_table_lookup(proxy->prop_list, name);
    if (prop == NULL)
        return FALSE;

    if (prop->msg == NULL)
        return FALSE;

    if (dbus_message_iter_init(prop->msg, iter) == FALSE)
        return FALSE;

    return TRUE;
}

// bluez/gdbus/object.c
static void g_dbus_flush(DBusConnection *connection)
{
    GSList *l;

    for (l = pending; l;) {
        struct generic_data *data = l->data;

        l = l->next;
        if (data->conn != connection)
            continue;

        process_changes(data);
    }
}

// bluez/gdbus/object.c
gboolean g_dbus_send_message_with_reply(DBusConnection *connection,
                    DBusMessage *message,
                    DBusPendingCall **call, int timeout)
{
    dbus_bool_t ret;

    /* Flush pending signal to guarantee message order */
    g_dbus_flush(connection);

    ret = dbus_connection_send_with_reply(connection, message, call,
                                timeout);

    if (ret == TRUE && call != NULL && *call == NULL) {
        error("Unable to send message (passing fd blocked?)");
        return FALSE;
    }

    return ret;
}

// bluez/gdbus/object.c
static void process_properties_from_interface(struct generic_data *data,
                        struct interface_data *iface)
{
    GSList *l;
    DBusMessage *signal;
    DBusMessageIter iter, dict, array;
    GSList *invalidated;

    data->pending_prop = FALSE;

    if (iface->pending_prop == NULL)
        return;

    signal = dbus_message_new_signal(data->path,
            DBUS_INTERFACE_PROPERTIES, "PropertiesChanged");
    if (signal == NULL) {
        error("Unable to allocate new " DBUS_INTERFACE_PROPERTIES
                        ".PropertiesChanged signal");
        return;
    }

    iface->pending_prop = g_slist_reverse(iface->pending_prop);

    dbus_message_iter_init_append(signal, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,    &iface->name);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
            DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
            DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
            DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

    invalidated = NULL;

    for (l = iface->pending_prop; l != NULL; l = l->next) {
        GDBusPropertyTable *p = l->data;

        if (p->get == NULL)
            continue;

        if (p->exists != NULL && !p->exists(p, iface->user_data)) {
            invalidated = g_slist_prepend(invalidated, p);
            continue;
        }

        append_property(iface, p, &dict);
    }

    dbus_message_iter_close_container(&iter, &dict);

    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
                DBUS_TYPE_STRING_AS_STRING, &array);
    for (l = invalidated; l != NULL; l = g_slist_next(l)) {
        GDBusPropertyTable *p = l->data;

        dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING,
                                &p->name);
    }
    g_slist_free(invalidated);
    dbus_message_iter_close_container(&iter, &array);

    g_slist_free(iface->pending_prop);
    iface->pending_prop = NULL;

    /* Use dbus_connection_send to avoid recursive calls to g_dbus_flush */
    dbus_connection_send(data->conn, signal, NULL);
    dbus_message_unref(signal);
}

int main(int argc, char *argv[])
{
    char *arg = argv[1];

    GDBusProxy *proxy;

    if (!arg || !strlen(arg)) {
        printf("Missing device address argument\n");
        return;
    }

    proxy = find_proxy_by_address(dev_list, arg);
    if (!proxy) {
        printf("Device %s not available\n", arg);
        return -1;
    }

    if (g_dbus_proxy_method_call(proxy, "Connect", NULL, connect_reply,
                            NULL, NULL) == FALSE) {
        printf("Failed to connect\n");
        return -1;
    }

    printf("Attempting to connect to %s\n", arg);

    return 0;
}

