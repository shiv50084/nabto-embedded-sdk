#ifndef _NP_MDNS_WRAPPER_H_
#define _NP_MDNS_WRAPPER_H_

#include "interfaces/np_mdns.h"

void np_mdns_publish_service(struct np_mdns* obj, uint16_t port, const char* productId, const char* deviceId);


#endif
