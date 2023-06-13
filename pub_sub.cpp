
#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cctype>
#include <thread>
#include <chrono>
#include <memory>
#include "mqtt/async_client.h"

using namespace std;
using namespace std::chrono;

const std::string DFLT_SERVER_ADDRESS("tcp://localhost:1883");
const std::string CLIENT_ID("multithr_pub_sub_cpp");


// The MQTT publisher function will run in its own thread.
// It runs until the receiver thread closes the counter object.
void publisher_func(mqtt::async_client_ptr cli)
{
	while (true) {
		auto qos = 0;
        string payload = "hello\n";
		cli->publish("events/count", payload, 0, false)->wait();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "published hello \n";
	}
}

/////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
	 string address = (argc > 1) ? string(argv[1]) : DFLT_SERVER_ADDRESS;

	// Create an MQTT client using a smart pointer to be shared among threads.
	auto cli = std::make_shared<mqtt::async_client>(address, CLIENT_ID);

	// Connect options for a persistent session and automatic reconnects.
	auto connOpts = mqtt::connect_options_builder()
		.clean_session(true)
		.automatic_reconnect(seconds(2), seconds(30))
		.finalize();

	auto TOPICS = mqtt::string_collection::create({ "data/#", "command" });
	const vector<int> QOS { 0, 1 };

	try {
		// Start consuming _before_ connecting, because we could get a flood
		// of stored messages as soon as the connection completes since
		// we're using a persistent (non-clean) session with the broker.
		cli->start_consuming();

		cout << "Connecting to the MQTT server at " << address << "..." << flush;
		auto rsp = cli->connect(connOpts)->get_connect_response();
		cout << "OK\n" << endl;

		// Subscribe if this is a new session with the server
		if (!rsp.is_session_present())
			cli->subscribe(TOPICS, QOS);

		// Start the publisher thread

		std::thread publisher(publisher_func, cli);

		// Consume messages in this thread

		while (true) {
			auto msg = cli->consume_message();

			// if (!msg)
			// 	continue;

			if (msg->get_topic() == "command" &&
					msg->to_string() == "exit") {
				cout << "Exit command received" << endl;
				break;
			}

			cout << msg->get_topic() << ": " << msg->to_string() << endl;
		}

		// Close the counter and wait for the publisher thread to complete
		cout << "\nShutting down..." << flush;
		publisher.join();

		// Disconnect

		cout << "OK\nDisconnecting..." << flush;
		cli->disconnect();
		cout << "OK" << endl;
	}
	catch (const mqtt::exception& exc) {
		cerr << exc.what() << endl;
		return 1;
	}

 	return 0;
}