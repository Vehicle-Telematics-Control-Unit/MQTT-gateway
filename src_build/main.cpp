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
#include <optional>

#include "mqtt/async_client.h"
#include "VsomeipAgent.hpp"
#include "nlohmann/json.hpp"


#define TESTING_MODE 1

static constexpr char DFLT_SERVER_ADDRESS[] = "tcp://localhost:1883";
static constexpr char CLIENT_ID[] = "multithr_pub_sub_cpp";

std::optional<std::string> requestMQTTcredentials(std::shared_ptr<VsomeipAgent> vsomeipagent)
{
    // while(! vsomeipagent->serviceAvailability[BACKEND_REQUEST_SERVER_SERVICE_ID])
    //     std::this_thread::sleep_for(std::chrono::seconds(2));
    
    using json = nlohmann::json;
    // TODO: change this to meaningful data
    json jsonMessage = {
        {"route", "authentication/mqtt/getCredentials/TCU"},
        {"vin", "12"}
    };
    std::string dataToSend = jsonMessage.dump(3);
    vsomeipagent->sendToBackEndGateWay(dataToSend);
    // wait till it gets updated
    const uint8_t timeoutSeconds = 1;
    auto start = std::chrono::steady_clock::now();
    while (!vsomeipagent->getBackEndResponse().second)
    {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        if (duration.count() >= timeoutSeconds)
        {
            // Timeout return empty credentials
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // return the credentials
    return vsomeipagent->getBackEndResponse().first;
}

void gpsPublisher(std::shared_ptr<mqtt::async_client> client, std::string gpsTopic,std::shared_ptr<VsomeipAgent> vsomeipagent, std::shared_ptr<std::atomic<bool>> transmissionState)
{
    while (true)
    {
        if (transmissionState->load())
        {
            // TODO: publish to a real topic
            #if TESTING_MODE
            std::string data = vsomeipagent->getGpsCoordinates();
            if(data == "")
            {
                data = " \"lat\": 30.037064667, \"lng\": 31.400575333 ";
            }
            client->publish(gpsTopic, data, 0, false)->wait();
            #else
            client->publish(gpsTopic, vsomeipagent->getGpsCoordinates(), 0, false)->wait();
            #endif
        }
        else
        {
            std::cout << "\n\n\n stopping transmission of GPS! \n\n\n";
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

    std::optional<std::string> backendResponse;
    while(!backendResponse.has_value())
        backendResponse = requestMQTTcredentials(vsomeipagent);

    std::cout << "cred " << backendResponse.value() << '\n';
    std::string address = DFLT_SERVER_ADDRESS;

    using json = nlohmann::json;
    json credentials = json::parse(backendResponse.value());

    // TODO: check client ID
    // Create an MQTT client using a smart pointer to be shared among threads.
    #if TESTING_MODE == 0
    auto client = std::make_shared<mqtt::async_client>(address, CLIENT_ID);
    #else
    auto client = std::make_shared<mqtt::async_client>(address, "tester_gps_mqtt");
    #endif
    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(true)
                        #if TESTING_MODE == 0
                        .user_name(credentials["username"])
                        .password(credentials["pwd"])
                        #endif
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

                std::string gpsTopic = msg->get_payload();
                std::cout << "gps topic: " << gpsTopic << '\n';
                if (transmissionState->load() == false)
                {
                    transmissionState->store(true);
                    // TODO: insert real username and password
                    // std::pair<std::string, std::string> credentials = std::make_pair("username___", "password____");
                    if (gpsMQTTthread.joinable())
                    {
                        gpsMQTTthread.join();
                        gpsMQTTthread = std::thread(gpsPublisher, client, gpsTopic, vsomeipagent, transmissionState);
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
        using json = nlohmann::json;

        // while(1)
        // {
        //     // json jsonRequest;
        //     // jsonRequest["route"] = "abc";
        //     // jsonRequest["data"] = "aaabc";
        //     // std::string aa = jsonRequest.dump(4);
        //     // vsomeipAgnet->sendToBackEndGateWay(aa);
        //     std::optional<std::string> credential;
        //     while(!credential.has_value())
        //         credential = requestMQTTcredentials(vsomeipAgnet);

        //     std::cout << "cred " << credential.value() << '\n';
        //     std::this_thread::sleep_for(std::chrono::seconds(2));
        // }
        t.join();
        mqttThread.join();
        return 0;
    }
    else
    {
        return 1;
    }
}