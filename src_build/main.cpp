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


#define TESTING_MODE 0

static constexpr char DFLT_SERVER_ADDRESS[] = "tcp://vehicleplus.cloud:1883";

std::optional<std::string> requestMQTTcredentials(std::shared_ptr<VsomeipAgent> vsomeipagent)
{
    static int count = 0;
    std::cout << "\n >>> ATEMPT NO " << count++ << '\n';
    // while(! vsomeipagent->serviceAvailability[BACKEND_REQUEST_SERVER_SERVICE_ID])
    //     std::this_thread::sleep_for(std::chrono::seconds(2));
    
    using json = nlohmann::json;
    // TODO: change this to meaningful data
    json jsonMessage = {
        {"route", "authentication/mqtt/getCredentials/TCU"},
        {"vin", "12"}
    };
    std::string dataToSend = jsonMessage.dump();
    vsomeipagent->sendToBackEndGateWay(dataToSend);
    // wait till it gets updated
    const uint8_t timeoutSeconds = 10;
    auto start = std::chrono::steady_clock::now();
    while (!vsomeipagent->getBackEndResponse().second)
    {
        // auto end = std::chrono::steady_clock::now();
        // auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        // if (duration.count() >= timeoutSeconds)
        // {
        //     // Timeout return empty credentials
        //     return std::nullopt;
        // }
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
            
            #if TESTING_MODE
            std::string data = vsomeipagent->getGpsCoordinates();
            if(data == "")
            {
                data = " \"lat\": 30.037064667, \"lng\": 31.400575333 ";
            }
            client->publish(gpsTopic, data, 0, false)->wait();
            #else
            std::string GPSdataStr = vsomeipagent->getGpsCoordinates();
            if(GPSdataStr != "")
            {
                client->publish(gpsTopic, GPSdataStr, 0, false)->wait();
                std::cout << "Sent: "<< GPSdataStr << '\n';
            }
            #endif
            std::cout << "published data!\n";
        }
        else
        {
            std::cout << "\n\n\n stopping transmission of GPS! \n\n\n";
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void mqttHandler(std::shared_ptr<VsomeipAgent> vsomeipagent)
{
    using namespace std;
    using namespace std::chrono;

    // TODO: fix backend gateway not responding
    // std::optional<std::string> backendResponse;
    // while(!backendResponse.has_value())
    //     backendResponse = requestMQTTcredentials(vsomeipagent);

    // std::cout << "cred " << backendResponse.value() << '\n';
    std::string address = DFLT_SERVER_ADDRESS;
    using json = nlohmann::json;
    // json credentials = json::parse(backendResponse.value());
    json credentials;

    // TODO: check json data
    credentials["clientId"] = "TCU-1";
    credentials["userName"] = "test_name";
    credentials["password"] = "test_pwd";
    #if TESTING_MODE
    credentials["clientId"] = "test_mqtt_gps";
    credentials["userName"] = "test_name";
    credentials["password"] = "test_pwd";
    #endif
    auto client = std::make_shared<mqtt::async_client>(address, credentials["clientId"]);
    auto connOpts = mqtt::connect_options_builder()
                        .clean_session(true)
                        .user_name(credentials["userName"])
                        .password(credentials["password"])
                        .automatic_reconnect(seconds(2), seconds(30))
                        .finalize();

    const std::string startGPStopic = std::string(credentials["clientId"]) + "/sendGPS";
    const std::string endGPStopic = std::string(credentials["clientId"]) + "/stopGPS";

    auto TOPICS = mqtt::string_collection::create({startGPStopic, endGPStopic});
    const vector<int> QOS{0, 1};
    std::shared_ptr<std::atomic<bool>> transmissionState = std::make_shared<std::atomic<bool>>(false);
    std::thread gpsMQTTthread([]() {});

    try
    {
        client->start_consuming();

        cout << "Connecting to the MQTT server at " << address << "...\n" << flush;
        auto rsp = client->connect(connOpts)->get_connect_response();
        cout << "Done connected \n" << flush;
        // Subscribe if this is a new session with the server
        if (!rsp.is_session_present())
            client->subscribe(TOPICS, QOS);

        // Consume messages in this thread
        while (true)
        {
            auto msg = client->consume_message();
            if (msg->get_topic() == startGPStopic)
            {
                std::string gpsTopic = msg->get_payload();
                std::cout << "gps topic: " << gpsTopic << '\n';
                if (transmissionState->load() == false)
                {
                    transmissionState->store(true);
                    if (gpsMQTTthread.joinable())
                    {
                        gpsMQTTthread.join();
                        gpsMQTTthread = std::thread(gpsPublisher, client, gpsTopic, vsomeipagent, transmissionState);
                        std::cout << "GPS publishing started!\n";

                    }
                }
            }
            else if (msg->get_topic() == endGPStopic)
            {
                transmissionState->store(false);
                std::cout << "GPS publishing stopped!\n";
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