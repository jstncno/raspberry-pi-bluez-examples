#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

// source adapted from:
// http://www.drdobbs.com/mobile/using-bluetooth/232500828?pgno=2
// http://people.csail.mit.edu/albert/bluez-intro/x604.html and
// http://nohands.sourceforge.net/source.html (libhfp/hfp.cpp: SdpRegister)
 
uint8_t channel = 3;
 
int main()
{
    const char *service_name = "HSP service";
    const char *service_dsc = "HSP";
    const char *service_prov = "Justin Cano";
 
    uuid_t hs_uuid, ga_uuid;
 
    sdp_profile_desc_t desc;
 
    uuid_t root_uuid, l2cap_uuid, rfcomm_uuid;
    sdp_list_t *l2cap_list = 0,
               *rfcomm_list = 0,
               *root_list = 0,
               *proto_list = 0,
               *access_proto_list = 0;
 
    sdp_data_t *channel_d = 0;
 
    int err = 0;
    sdp_session_t *session = 0;
 
    sdp_record_t *record = sdp_record_alloc();
 
    // set the name, provider, and description
    sdp_set_info_attr(record, service_name, service_prov, service_dsc);
 
    // service class ID (HEADSET)
    sdp_uuid16_create(&hs_uuid, HEADSET_SVCLASS_ID);
 
    if (!(root_list = sdp_list_append(0, &hs_uuid)))
        return -1;
 
    sdp_uuid16_create(&ga_uuid, GENERIC_AUDIO_SVCLASS_ID);
 
    if (!(root_list = sdp_list_append(root_list, &ga_uuid)))
        return -1;
 
    if (sdp_set_service_classes(record, root_list) < 0)
        return -1;
 
    sdp_list_free(root_list, 0);
    root_list = 0;
 
    // make the service record publicly browsable
    sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
 
    root_list = sdp_list_append(0, &root_uuid);
    sdp_set_browse_groups( record, root_list );
 
    // set l2cap information
    sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
    l2cap_list = sdp_list_append( 0, &l2cap_uuid );
    proto_list = sdp_list_append( 0, l2cap_list );
 
    // set rfcomm information
    sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
    channel_d = sdp_data_alloc(SDP_UINT8, &channel);
    rfcomm_list = sdp_list_append( 0, &rfcomm_uuid );
 
    sdp_list_append( rfcomm_list, channel_d );
    sdp_list_append( proto_list, rfcomm_list );
 
    // attach protocol information to service record
    access_proto_list = sdp_list_append( 0, proto_list );
    sdp_set_access_protos( record, access_proto_list );
 
    sdp_uuid16_create(&desc.uuid, HEADSET_PROFILE_ID);
 
    // set the version to 1.0
    desc.version = 0x0100;
 
    if (!(root_list = sdp_list_append(NULL, &desc)))
        return -1;
 
    if (sdp_set_profile_descs(record, root_list) < 0)
        return -1;
 
    // connect to the local SDP server and register the service record
    session = sdp_connect( BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY );
    err = sdp_record_register(session, record, 0);
 
    // cleanup
    sdp_data_free( channel_d );
    sdp_list_free( l2cap_list, 0 );
    sdp_list_free( rfcomm_list, 0 );
    sdp_list_free( root_list, 0 );
    sdp_list_free( access_proto_list, 0 );
 
    while (1)
        sleep(5000);
 
    return err;
}
