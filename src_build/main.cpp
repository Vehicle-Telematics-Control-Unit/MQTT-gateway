#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cctype>
#include <memory>
#include <atomic>
#include <future>
#include "vsomeip/vsomeip.hpp"
#include "mqtt/async_client.h"

#include "mqtt_vsome_ip_conf.hpp"
#include "nlohmann/json.hpp"

// How many to buffer while off-line
const int MAX_BUFFERED_MESSAGES = 1200;

static constexpr char DFLT_SERVER_ADDRESS[] = "tcp://localhost:1883";
static constexpr char CLIENT_ID[] = "multithr_pub_sub_cpp";

class VsomeipAgent
{
public:
    VsomeipAgent() : app_(vsomeip::runtime::get()->create_application())
    {
    }

    bool init()
    {
        if (!app_->init())
        {
            std::cerr << "Couldn't initialize application" << std::endl;
            return false;
        }
        std::cout << "Client settings [protocol="
                  << (use_tcp_ ? "TCP" : "UDP")
                  << "]"
                  << std::endl;

        app_->register_state_handler(
            std::bind(&VsomeipAgent::on_state, this,
                      std::placeholders::_1));

        app_->register_message_handler(
            BACKEND_REQUEST_SERVER_SERVICE_ID, BACKEND_REQUEST_SERVER_INSTANCE_ID, BACKEND_REQUEST_SERVER_METHOD_ID,
            [this](const std::shared_ptr<vsomeip::message> &_response)
            {
                std::string response((char *)_response->get_payload()->get_data(), _response->get_payload()->get_length());
                m_backEndResponseUpdate = {response, true};
            });

        app_->register_availability_handler(BACKEND_REQUEST_SERVER_SERVICE_ID, BACKEND_REQUEST_SERVER_INSTANCE_ID,
                                            std::bind(&VsomeipAgent::on_availability,
                                                      this,
                                                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        app_->register_message_handler(
            vsomeip::ANY_SERVICE, GNSS_REQUEST_INSTANCE_ID, vsomeip::ANY_METHOD,
            std::bind(&VsomeipAgent::gpsEventHandle, this,
                      std::placeholders::_1));

        app_->register_availability_handler(GNSS_REQUEST_SERVICE_ID, GNSS_REQUEST_INSTANCE_ID,
                                            std::bind(&VsomeipAgent::on_availability,
                                                      this,
                                                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        return true;
    }

    const std::pair<std::string, bool> &getBackEndResponse()
    {
        return m_backEndResponseUpdate;
    }

    void start()
    {
        app_->start();
    }

#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
    /*
     * Handle signal to shutdown
     */
    void stop()
    {
        app_->clear_all_handler();
        app_->unsubscribe(GNSS_REQUEST_SERVICE_ID, GNSS_REQUEST_INSTANCE_ID, GNSS_REQUEST_EVENTGROUP_ID);
        app_->release_event(GNSS_REQUEST_SERVICE_ID, GNSS_REQUEST_INSTANCE_ID, GNSS_REQUEST_EVENT_ID);
        app_->release_service(GNSS_REQUEST_SERVICE_ID, GNSS_REQUEST_INSTANCE_ID);
        app_->stop();
    }
#endif

    void on_state(vsomeip::state_type_e _state)
    {
        if (_state == vsomeip::state_type_e::ST_REGISTERED)
        {
            app_->request_service(GNSS_REQUEST_SERVICE_ID, GNSS_REQUEST_INSTANCE_ID);
            std::set<vsomeip::eventgroup_t> its_groups;
            its_groups.insert(GNSS_REQUEST_EVENTGROUP_ID);
            app_->request_event(
                GNSS_REQUEST_SERVICE_ID,
                GNSS_REQUEST_INSTANCE_ID,
                GNSS_REQUEST_EVENT_ID,
                its_groups,
                vsomeip::event_type_e::ET_FIELD);
            app_->subscribe(GNSS_REQUEST_SERVICE_ID, GNSS_REQUEST_INSTANCE_ID, GNSS_REQUEST_EVENTGROUP_ID);
        }
        m_state = _state;
    }

    bool getAppState()
    {
        return (m_state == vsomeip::state_type_e::ST_REGISTERED);
    }

    void on_availability(vsomeip::service_t _service, vsomeip::instance_t _instance, bool _is_available)
    {
        std::cout << "Service ["
                  << std::setw(4) << std::setfill('0') << std::hex << _service << "." << _instance
                  << "] is "
                  << (_is_available ? "available." : "NOT available.")
                  << std::endl;
    }

    void gpsEventHandle(const std::shared_ptr<vsomeip::message> &_response)
    {
        // std::stringstream its_message;
        std::string str((char *)_response->get_payload()->get_data(), _response->get_payload()->get_length());
        std::cout << str << "\n";
        std::cout << _response->get_client();
        std::lock_guard<std::mutex> lock(m_gps_mutex);
        m_gpsdata = std::move(str);
    }

    std::string getGpsCoordinates()
    {
        std::lock_guard<std::mutex> lock(m_gps_mutex);
        return m_gpsdata;
    }
    void sendToBackEndGateWay(const std::string &msg)
    {
        m_backEndResponseUpdate.second = false;
        m_backEndResponseUpdate.first = "";
        std::shared_ptr<vsomeip::message> request_(vsomeip::runtime::get()->create_request(false));
        request_->set_service(BACKEND_REQUEST_SERVER_SERVICE_ID);
        request_->set_instance(BACKEND_REQUEST_SERVER_INSTANCE_ID);
        request_->set_method(BACKEND_REQUEST_SERVER_METHOD_ID);

        std::shared_ptr<vsomeip::payload> its_payload = vsomeip::runtime::get()->create_payload();
        std::vector<vsomeip::byte_t> its_payload_data(msg.begin(), msg.end());

        its_payload->set_data(its_payload_data);
        request_->set_payload(its_payload);
        app_->send(request_);
    }

private:
    std::shared_ptr<vsomeip::application> app_;
    bool use_tcp_;
    std::pair<std::string, bool> m_backEndResponseUpdate;
    std::string m_gpsdata = "Lat: xx, Long: xx";
    std::mutex m_gps_mutex;
    vsomeip::state_type_e m_state;
};

std::string requestMQTTcredentials(std::shared_ptr<VsomeipAgent> vsomeipagent)
{
    std::this_thread::sleep_for(std::chrono::seconds(2));
    using json = nlohmann::json;
    json jsonMessage = {
        {"route", "mqtt/credentials"},
        {"vin", "12334"},
    };
    vsomeipagent->sendToBackEndGateWay(jsonMessage.dump(3));
    // wait till it gets updated
    const uint8_t timeoutSeconds = 5;
    auto start = std::chrono::steady_clock::now();
    while (!vsomeipagent->getBackEndResponse().second)
    {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        if (duration.count() >= timeoutSeconds)
        { // Timeout occurred
            return "";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // return the credentials
    return vsomeipagent->getBackEndResponse().first;
}

void gpsPublisher(std::pair<std::string, std::string> credentials, std::shared_ptr<VsomeipAgent> vsomeipagent, std::shared_ptr<std::atomic<bool>> transmissionState)
{
    using namespace std;
    using namespace std::chrono;
    std::string address = DFLT_SERVER_ADDRESS;

    auto client = std::make_shared<mqtt::async_client>(address, "abcd");

    // Connect options for a persistent session and automatic reconnects.
    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(false)
                        .user_name(credentials.first)
                        .password(credentials.second)
                        .automatic_reconnect(seconds(2), seconds(30))
                        .finalize();

    // auto TOPICS = mqtt::string_collection::create({"f/#", "ff"});
    // const vector<int> QOS{0, 1};

    // of stored messages as soon as the connection completes since
    // we're using a persistent (non-clean) session with the broker.
    client->start_consuming();

    cout << "Connecting to the MQTT server at " << address << "..." << flush;
    auto rsp = client->connect(connOpts)->get_connect_response();

    // if (!rsp.is_session_present())
    //     client->subscribe(TOPICS, QOS);

    while (true)
    {
        auto qos = 0;
        if (transmissionState->load())
            client->publish("events/count", vsomeipagent->getGpsCoordinates(), 0, false)->wait();
        else
        {
            std::cout << "\n\n\n stopping transmission of GPS! \n\n\n";
            client->disconnect();
            return;
        }
        std::cout << "published hello \n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void mqttHandler(std::shared_ptr<VsomeipAgent> vsomeipagent)
{
    using namespace std;
    using namespace std::chrono;
    std::string address = DFLT_SERVER_ADDRESS;

    // Create an MQTT client using a smart pointer to be shared among threads.
    auto client = std::make_shared<mqtt::async_client>(address, CLIENT_ID);

    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(true)
                        .automatic_reconnect(seconds(2), seconds(30))
                        .finalize();

    auto TOPICS = mqtt::string_collection::create({"data/#", "request/gps/+"});
    const vector<int> QOS{0, 1};
    std::shared_ptr<std::atomic<bool>> transmissionState = std::make_shared<std::atomic<bool>>(false);
    std::thread gpsMQTTthread([]() {});

    try
    {
        client->start_consuming();

        cout << "Connecting to the MQTT server at " << address << "..." << flush;
        auto rsp = client->connect(connOpts)->get_connect_response();

        // Subscribe if this is a new session with the server
        if (!rsp.is_session_present())
            client->subscribe(TOPICS, QOS);

        // std::thread publisher(publisher_func, vsomeipagent);

        // Consume messages in this thread
        while (true)
        {
            auto msg = client->consume_message();
            if (msg->get_topic() == "request/gps/start")
            {
                std::string credentials = requestMQTTcredentials(vsomeipagent);
                std::cout << "GOT the credentials: " << credentials << '\n';
                if (credentials == "")
                {
                    std::cout << "failed to get credentials\n";
                }
                else
                {
                    if (transmissionState->load() == false)
                    {
                        transmissionState->store(true);
                        std::pair<std::string, std::string> credentials = std::make_pair("username___", "password____");
                        if (gpsMQTTthread.joinable())
                        {
                            gpsMQTTthread.join();
                            gpsMQTTthread = std::thread(gpsPublisher, credentials, vsomeipagent, transmissionState);
                        }
                    }
                }
            }
            else if (msg->get_topic() == "request/gps/stop")
            {
                transmissionState->store(false);
            }
            cout << msg->get_topic() << ": " << msg->to_string() << endl;
        }

        gpsMQTTthread.join();
        // Close the counter and wait for the publisher thread to complete
        cout << "\nShutting down..." << flush;
        // publisher.join();

        // Disconnect

        cout << "OK\nDisconnecting..." << flush;
        client->disconnect();
        cout << "OK" << endl;
    }
    catch (const mqtt::exception &exc)
    {
        cerr << exc.what() << endl;
        return;
    }
}


int main(int argc, char **argv)
{
    std::shared_ptr<VsomeipAgent> vsomeipAgnet = std::make_shared<VsomeipAgent>();

    if (vsomeipAgnet->init())
    {
        std::thread t([&]
                      { vsomeipAgnet->start(); });

        std::thread mqttThread([&]()
                               { mqttHandler(vsomeipAgnet); });

        t.join();
        mqttThread.join();
        return 0;
    }
    else
    {
        return 1;
    }
}