#include <string>
#include <memory>
#include <atomic>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <map>
#include "vsomeip/vsomeip.hpp"

#include "mqtt_vsome_ip_conf.hpp"

class VsomeipAgent
{
public:
    VsomeipAgent() : app_(vsomeip::runtime::get()->create_application()), m_gpsdata{""}
    {
    }

    bool init()
    {
        if (!app_->init())
        {
            std::cerr << "Couldn't initialize application" << '\n';
            return false;
        }
        
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

            // BACKEND_SERVER_CHECK_INTERNET_CONNECTION_METHOD_ID

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
                  << _service << "." << _instance
                  << "] is "
                  << (_is_available ? "available." : "NOT available.")
                  << '\n';
        serviceAvailability[_service] = _is_available;
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

public:
    std::map<vsomeip::service_t, bool> serviceAvailability;
private:
    std::shared_ptr<vsomeip::application> app_;
    bool use_tcp_;
    std::pair<std::string, bool> m_backEndResponseUpdate;
    std::string m_gpsdata;
    std::mutex m_gps_mutex;
    vsomeip::state_type_e m_state;
};