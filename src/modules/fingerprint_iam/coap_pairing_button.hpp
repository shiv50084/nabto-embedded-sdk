#pragma once

#include <nabto/nabto_device.h>

#include "coap_request_handler.hpp"

#include <functional>

namespace nabto {
namespace fingerprint_iam {

class CoapPairingButton : public CoapRequestHandler {
 public:
    CoapPairingButton(FingerprintIAM& iam, NabtoDevice* device)
        : CoapRequestHandler(iam, device)
    {
    }

    bool init(std::function<void (std::string fingerprint, std::function<void (bool accepted)> cb)> callback)
    {
        callback_ = callback;
        return CoapRequestHandler::init(NABTO_DEVICE_COAP_POST, {"pairing", "button"});
    }

    virtual void handleRequest(NabtoDeviceCoapRequest* request)
    {
        NabtoDeviceConnectionRef ref = nabto_device_coap_request_get_connection_ref(request);
        if (!iam_.checkAccess(ref, "Pairing:Button")) {
            nabto_device_coap_error_response(request, 403, "Access Denied");
            nabto_device_coap_request_free(request);
            return;
        }

        NabtoDeviceError ec;
        char* fingerprint;
        ec = nabto_device_connection_get_client_fingerprint_hex(getDevice(), ref, &fingerprint);
        if (ec) {
            nabto_device_coap_error_response(request, 500, "Server error");
            nabto_device_coap_request_free(request);
            return;
        }
        std::string clientFingerprint(fingerprint);
        nabto_device_string_free(fingerprint);

        callback_(clientFingerprint, [request, this, clientFingerprint](bool accepted){
                if (!accepted) {
                    nabto_device_coap_error_response(request, 403, "Access denied");
                    nabto_device_coap_request_free(request);
                    return;
                }
                if (!iam_.pairNewClient(clientFingerprint)) {
                    std::cout << "Could not pair the user" << std::endl;
                    nabto_device_coap_error_response(request, 500, "Server error");
                    nabto_device_coap_request_free(request);
                    return;
                }

                std::cout << "Paired the user with the fingerprint " << clientFingerprint << std::endl;
                // OK response
                nabto_device_coap_response_set_code(request, 201);
                nabto_device_coap_response_ready(request);
                nabto_device_coap_request_free(request);
            });
    }
 private:
    std::function<void (std::string fingerprint, std::function<void (bool accepted)> cb)> callback_;
};

} } // namespace
