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

#include "mqtt/async_client.h"

#include "VsomeipAgent.hpp"
#include "nlohmann/json.hpp"

static constexpr char DFLT_SERVER_ADDRESS[] = "tcp://localhost:1883";
static constexpr char CLIENT_ID[] = "multithr_pub_sub_cpp";

std::string requestMQTTcredentials(std::shared_ptr<VsomeipAgent> vsomeipagent)
{
    std::this_thread::sleep_for(std::chrono::seconds(2));
    using json = nlohmann::json;
    // TODO: change this to meaningful data
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
        {
            // Timeout return empty credentials
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
    // TODO: change this to a real client ID
    auto client = std::make_shared<mqtt::async_client>(DFLT_SERVER_ADDRESS, "abcd");

    // Connect options for a persistent session and automatic reconnects.
    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(false)
                        .user_name(credentials.first)
                        .password(credentials.second)
                        .automatic_reconnect(seconds(2), seconds(30))
                        .finalize();

    client->start_consuming();

    cout << "Connecting to the MQTT server at " << DFLT_SERVER_ADDRESS << "..." << flush;
    auto rsp = client->connect(connOpts)->get_connect_response();

    while (true)
    {
        if (transmissionState->load())
            // TODO: publish to a real topic
            client->publish("events/gps", vsomeipagent->getGpsCoordinates(), 0, false)->wait();
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

    // TODO: check client ID
    // Create an MQTT client using a smart pointer to be shared among threads.
    auto client = std::make_shared<mqtt::async_client>(address, CLIENT_ID);

    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(true)
                        .automatic_reconnect(seconds(2), seconds(30))
                        .finalize();

    // TODO: subscribe to real topics
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

        // Consume messages in this thread
        while (true)
        {
            auto msg = client->consume_message();

            // TODO: insert real Topic and change to static constexpr
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
                        // TODO: insert real username and password
                        std::pair<std::string, std::string> credentials = std::make_pair("username___", "password____");
                        if (gpsMQTTthread.joinable())
                        {
                            gpsMQTTthread.join();
                            gpsMQTTthread = std::thread(gpsPublisher, credentials, vsomeipagent, transmissionState);
                        }
                    }
                }
            }
            // TODO: insert real Topic and change to static constexpr
            else if (msg->get_topic() == "request/gps/stop")
            {
                transmissionState->store(false);
            }
            cout << msg->get_topic() << ": " << msg->to_string() << '\n';
        }

        gpsMQTTthread.join();
        cout << "\n\nDisconnecting..." << flush;
        client->disconnect();
        cout << "OK" << '\n';
    }
    catch (const mqtt::exception &exc)
    {
        cerr << exc.what() << '\n';
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