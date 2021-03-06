#include <boost/test/unit_test.hpp>

#include <platform/np_event_queue_wrapper.h>

#include <core/nc_attacher.h>
#include <core/nc_device.h>

#include <util/io_service.hpp>
#include <fixtures/dtls_server/test_certificates.hpp>

#include "attach_server.hpp"
#include <test_platform.hpp>

#include <future>

namespace nabto {
namespace test {

class AttachTest {
 public:
    AttachTest(nabto::test::TestPlatform& tp, uint16_t port)
        : tp_(tp)
    {
        serverPort_ = port;
        struct np_platform* pl = tp_.getPlatform();
        np_completion_event_init(&pl->eq, &boundCompletionEvent, &AttachTest::udpDispatchCb, this);
    }

    ~AttachTest()
    {
        nc_attacher_deinit(&attach_);
        nc_coap_client_deinit(&coapClient_);
        nc_udp_dispatch_deinit(&udpDispatch_);
        np_completion_event_deinit(&boundCompletionEvent);
    }

    void start(std::function<void (AttachTest& at)> event, std::function<void (AttachTest& at)> state) {
        event_ = event;
        state_ = state;
        BOOST_TEST(nc_udp_dispatch_init(&udpDispatch_, tp_.getPlatform()) == NABTO_EC_OK);
        nc_udp_dispatch_async_bind(&udpDispatch_, tp_.getPlatform(), 0,
                                   &boundCompletionEvent);
    }

    void startAttach() {
        nc_coap_client_init(tp_.getPlatform(), &coapClient_);
        nc_attacher_init(&attach_, tp_.getPlatform(), &device_, &coapClient_, &AttachTest::listener, this);
        nc_attacher_set_state_listener(&attach_, &AttachTest::stateListener, this);
        nc_attacher_set_keys(&attach_,
                             reinterpret_cast<const unsigned char*>(nabto::test::devicePublicKey.c_str()), nabto::test::devicePublicKey.size(),
                             reinterpret_cast<const unsigned char*>(nabto::test::devicePrivateKey.c_str()), nabto::test::devicePrivateKey.size());
        nc_attacher_set_app_info(&attach_, appName_, appVersion_);
        nc_attacher_set_device_info(&attach_, productId_, deviceId_);
        // set timeout to approximately one seconds for the dtls handshake
        nc_attacher_set_handshake_timeout(&attach_, 50, 500);
        attach_.retryWaitTime = 100;
        attach_.accessDeniedWaitTime = 1000;

        BOOST_TEST(nc_attacher_start(&attach_, hostname_, serverPort_, &udpDispatch_) == NABTO_EC_OK);
    }

    void setDtlsPort(uint16_t port)
    {
        attach_.defaultPort = port;
    }

    void niceClose(std::function<void (AttachTest& at)> cb)
    {
        closed_ = cb;
        nc_attacher_async_close(&attach_, &AttachTest::closeCb, this);
    }

    static void closeCb(void* data)
    {
        AttachTest* at = (AttachTest*)data;
        if (!at->ended_)  {
            at->closed_(*at);
        }
    }

    static void stateListener(enum nc_attacher_attach_state state, void* data)
    {
        AttachTest* at = (AttachTest*)data;
        if (!at->ended_) {
            at->state_(*at);
        }
    }

    static void listener(enum nc_device_event event, void* data)
    {
        AttachTest* at = (AttachTest*)data;
        if (event == NC_DEVICE_EVENT_ATTACHED) {
            at->attachCount_++;
        } else if (event == NC_DEVICE_EVENT_DETACHED) {
            at->detachCount_++;
        }
        if (!at->ended_) {
            at->event_(*at);
        }
    }

    static void udpDispatchCb(const np_error_code ec, void* data) {
        BOOST_TEST(ec == NABTO_EC_OK);
        AttachTest* at = (AttachTest*)data;
        nc_udp_dispatch_start_recv(&at->udpDispatch_);
        at->startAttach();
    }

    void end() {
        nc_attacher_stop(&attach_);
        nc_udp_dispatch_abort(&udpDispatch_);
        testEnded_.set_value();
    }

    void waitForTestEnd() {
        std::future<void> fut = testEnded_.get_future();
        fut.get();
    }

    nabto::test::TestPlatform& tp_;
    struct nc_attach_context attach_;
    struct nc_device_context device_;
    struct nc_coap_client_context coapClient_;
    struct nc_udp_dispatch_context udpDispatch_;

    struct np_completion_event boundCompletionEvent;

    uint16_t serverPort_;
    const char* hostname_ = "localhost-multi.nabto.net";
    const char* appName_ = "foo";
    const char* appVersion_ = "bar";
    const char* productId_ = "test";
    const char* deviceId_ = "devTest";
    std::function<void (AttachTest& at)> event_;
    std::function<void (AttachTest& at)> state_;
    std::function<void (AttachTest& at)> closed_;
    bool ended_ = false;
    struct np_event* endEvent_;

    std::atomic<uint64_t> attachCount_ = { 0 };
    std::atomic<uint64_t> detachCount_ = { 0 };
    std::promise<void> testEnded_;

};

} }

BOOST_AUTO_TEST_SUITE(attach)

BOOST_AUTO_TEST_CASE(attach_close, * boost::unit_test::timeout(300))
{
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, attachServer->getPort());
    at.start([](nabto::test::AttachTest& at){
                 if (at.attachCount_ == (uint64_t)1) {
                     at.niceClose([](nabto::test::AttachTest& at) {
                                      at.end();
                                  });
                 }
             },[](nabto::test::AttachTest& at){ });

    at.waitForTestEnd();
    attachServer->stop();
    BOOST_TEST(attachServer->attachCount_ == (uint64_t)1);

    /******************************************************************
     * attachServer->stop() must invoke stop on the DTLS server from
     * the IO service. To avoid implementing a blocking test future we
     * stop the IO service nicely in all tests
     ******************************************************************/
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(attach_close_before_attach, * boost::unit_test::timeout(300))
{
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, attachServer->getPort());
    at.start([](nabto::test::AttachTest& at){ },[](nabto::test::AttachTest& at){
                 if (at.attach_.state == NC_ATTACHER_STATE_DTLS_ATTACH_REQUEST) {
                     at.niceClose([](nabto::test::AttachTest& at) {
                                      at.end();
                                  });
                 }
             });

    at.waitForTestEnd();
    attachServer->stop();

    /******************************************************************
     * attachServer->stop() must invoke stop on the DTLS server from
     * the IO service. To avoid implementing a blocking test future we
     * stop the IO service nicely in all tests
     ******************************************************************/
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(attach, * boost::unit_test::timeout(300))
{
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, attachServer->getPort());
    at.start([](nabto::test::AttachTest& at){
                 if (at.attachCount_ == (uint64_t)1) {
                     at.end();
                 }
             },[](nabto::test::AttachTest& at){ });

    at.waitForTestEnd();
    attachServer->stop();
    BOOST_TEST(attachServer->attachCount_ == (uint64_t)1);

    /******************************************************************
     * attachServer->stop() must invoke stop on the DTLS server from
     * the IO service. To avoid implementing a blocking test future we
     * stop the IO service nicely in all tests
     ******************************************************************/
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(detach, * boost::unit_test::timeout(300))
{
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());

    // means device detaches after ~200ms
    attachServer->setKeepAliveSettings(100, 50, 2);

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, attachServer->getPort());
    at.start([&attachServer](nabto::test::AttachTest& at){
            if (at.attachCount_ == 1 && at.detachCount_ == 0) {
                attachServer->stop();
            }
            if (at.attachCount_ == 1 &&
                at.detachCount_ == 1)
            {
                at.end();
            }
        },[](nabto::test::AttachTest& at){ });

    at.waitForTestEnd();
    attachServer->stop();
    BOOST_TEST(attachServer->attachCount_ == (uint64_t)1);
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(redirect, * boost::unit_test::timeout(300))
{
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());
    auto redirectServer = nabto::test::RedirectServer::create(ioService->getIoService());
    redirectServer->setRedirect("localhost-multi.nabto.net", attachServer->getPort(), attachServer->getFingerprint());
    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, redirectServer->getPort());
    at.start([](nabto::test::AttachTest& at){
                 if (at.attachCount_ == 1) {
                     at.end();
                 }
        },[](nabto::test::AttachTest& at){ });
    at.waitForTestEnd();
    attachServer->stop();
    redirectServer->stop();

    BOOST_TEST(attachServer->attachCount_ == (uint64_t)1);
    BOOST_TEST(redirectServer->redirectCount_ == (uint64_t)1);
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(reattach, * boost::unit_test::timeout(300))
{
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());

    // means device detaches after ~200ms
    attachServer->setKeepAliveSettings(100, 50, 2);

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, attachServer->getPort());
    at.start([&ioService, &attachServer](nabto::test::AttachTest& at){
            if (at.attachCount_ == 1 && at.detachCount_ == 0) {
                attachServer->stop();
                attachServer = nabto::test::AttachServer::create(ioService->getIoService());
                at.setDtlsPort(attachServer->getPort());
            }
            if (at.attachCount_ == 2 &&
                at.detachCount_ == 1)
            {
                at.end();
            }
        },[](nabto::test::AttachTest& at){ });
    at.waitForTestEnd();
    attachServer->stop();
    BOOST_TEST(at.attachCount_ == (uint64_t)2);
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(reattach_after_close_from_server, * boost::unit_test::timeout(300))
{
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, attachServer->getPort());
    at.start([&ioService, &attachServer](nabto::test::AttachTest& at){
            if (at.attachCount_ == 1 && at.detachCount_ == 0) {
                attachServer->niceClose();
            }
            if (at.attachCount_ == 1 &&
                at.detachCount_ == 1)
            {
                attachServer->stop();
                attachServer = nabto::test::AttachServer::create(ioService->getIoService());
                at.setDtlsPort(attachServer->getPort());
            }
            if (at.attachCount_ == 2 &&
                at.detachCount_ == 1)
            {
                at.end();
            }
        },[](nabto::test::AttachTest& at){ });
    at.waitForTestEnd();
    attachServer->stop();
    BOOST_TEST(at.attachCount_ == (uint64_t)2);
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(retry_after_server_unavailable, * boost::unit_test::timeout(300))
{
    // the device waits for dtls to timeout and retry again.
    auto ioService = nabto::IoService::create("test");
    std::shared_ptr<nabto::test::AttachServer> attachServer;

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, 4242);

    std::thread t([&ioService, &attachServer, &at](){
            std::this_thread::sleep_for(std::chrono::seconds(1));
            attachServer = nabto::test::AttachServer::create(ioService->getIoService());
            at.setDtlsPort(attachServer->getPort());
        });
    at.start([](nabto::test::AttachTest& at){
            if (at.attachCount_ == 1)
            {
                at.end();
            }
        },[](nabto::test::AttachTest& at){ });

    t.join();
    at.waitForTestEnd();
    attachServer->stop();

    BOOST_TEST(at.attachCount_ == (uint64_t)1);
    BOOST_TEST(attachServer->attachCount_ == (uint64_t)1);
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(reject_invalid_redirect, * boost::unit_test::timeout(300))
{
    // The redirect is invalid, go to retry
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());
    auto redirectServer = nabto::test::RedirectServer::create(ioService->getIoService());
    redirectServer->setRedirect("localhost-multi.nabto.net", attachServer->getPort(), attachServer->getFingerprint());
    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, redirectServer->getPort());

    redirectServer->invalidRedirect_ = 0;

    at.start([](nabto::test::AttachTest& at){
            if (at.attachCount_ == 1)
            {
                at.end();
            }
        },[](nabto::test::AttachTest& at){ });
    at.waitForTestEnd();
    attachServer->stop();
    redirectServer->stop();
    BOOST_TEST(attachServer->attachCount_ == (uint64_t)1);
    BOOST_TEST(redirectServer->redirectCount_ == (uint64_t)2);
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(reject_bad_coap_attach_response, * boost::unit_test::timeout(300))
{
    // The attach did not succeeed, go to retry
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, attachServer->getPort());

    attachServer->invalidAttach_ = 0;

    at.start([](nabto::test::AttachTest& at){
            if (at.attachCount_ == 1)
            {
                at.end();
            }
        },[](nabto::test::AttachTest& at){ });
    at.waitForTestEnd();
    attachServer->stop();
    BOOST_TEST(attachServer->attachCount_ == (uint64_t)2);
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(access_denied, * boost::unit_test::timeout(300))
{
    // The attach did not succeeed, go to retry
    auto ioService = nabto::IoService::create("test");
    auto accessDeniedServer = nabto::test::AccessDeniedServer::create(ioService->getIoService());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, accessDeniedServer->getPort());

    at.start([](nabto::test::AttachTest& at){ }, [](nabto::test::AttachTest& at){
                 if (at.attach_.state == NC_ATTACHER_STATE_ACCESS_DENIED_WAIT) {
                     at.end();
                 }
             });
    at.waitForTestEnd();
    accessDeniedServer->stop();
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(access_denied_reattach, * boost::unit_test::timeout(300))
{
    // The attach did not succeeed, go to retry
    auto ioService = nabto::IoService::create("test");
    auto accessDeniedServer = nabto::test::AccessDeniedServer::create(ioService->getIoService());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, accessDeniedServer->getPort());

    at.start([](nabto::test::AttachTest& at){ }, [&accessDeniedServer](nabto::test::AttachTest& at){
                 if (at.attach_.state == NC_ATTACHER_STATE_ACCESS_DENIED_WAIT &&
                     accessDeniedServer->coapRequestCount_ == 2) {
                     BOOST_TEST(at.attachCount_ == (uint64_t)0);
                     BOOST_TEST(accessDeniedServer->coapRequestCount_ == (uint64_t)2);
                     at.end();
                 }
             });
    at.waitForTestEnd();
    accessDeniedServer->stop();
    ioService->stop();
}

BOOST_AUTO_TEST_CASE(redirect_loop_break, * boost::unit_test::timeout(300))
{
    // The attach did not succeeed, go to retry
    auto ioService = nabto::IoService::create("test");

    auto redirectServer = nabto::test::RedirectServer::create(ioService->getIoService());
    redirectServer->setRedirect("localhost-multi.nabto.net", redirectServer->getPort(), redirectServer->getFingerprint());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, redirectServer->getPort());

    at.start([](nabto::test::AttachTest& at){ }, [](nabto::test::AttachTest& at){
                if (at.attach_.state == NC_ATTACHER_STATE_RETRY_WAIT) {
                    BOOST_TEST(at.attachCount_ == (uint64_t)0);
                    BOOST_TEST(at.attach_.state == NC_ATTACHER_STATE_RETRY_WAIT);
                    at.end();
                }
             });
    at.waitForTestEnd();
    redirectServer->stop();
    BOOST_TEST(redirectServer->redirectCount_ <= (uint64_t)5);
    ioService->stop();
}

#ifdef __linux__
BOOST_AUTO_TEST_CASE(attach_ha, * boost::unit_test::timeout(300))
{
    auto ioService = nabto::IoService::create("test");
    auto attachServer = nabto::test::AttachServer::create(ioService->getIoService(), "127.0.0.2", 0);
    auto attachServer2 = nabto::test::AttachServer::create(ioService->getIoService(), "127.0.0.1", attachServer->getPort());

    auto tp = nabto::test::TestPlatform::create();
    nabto::test::AttachTest at(*tp, attachServer->getPort());
    at.start([](nabto::test::AttachTest& at){
                 if (at.attachCount_ == (uint64_t)1) {
                     at.end();
                 }
             },[](nabto::test::AttachTest& at){
                   BOOST_TEST(at.attach_.state != NC_ATTACHER_STATE_RETRY_WAIT);
               });

    at.waitForTestEnd();
    attachServer->stop();
    attachServer2->stop();
    BOOST_TEST(attachServer->attachCount_+attachServer2->attachCount_ == (uint64_t)1);

    /******************************************************************
     * attachServer->stop() must invoke stop on the DTLS server from
     * the IO service. To avoid implementing a blocking test future we
     * stop the IO service nicely in all tests
     ******************************************************************/
    ioService->stop();
}
#endif


BOOST_AUTO_TEST_SUITE_END()
