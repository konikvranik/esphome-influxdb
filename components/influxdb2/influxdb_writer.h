#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include <utility>
#include <vector>

#include "esphome/components/http_request/http_request_idf.h"


namespace esphome::influxdb2
{
    class InfluxDBWriter : public Component
    {
    public:
        virtual ~InfluxDBWriter() = default;

        InfluxDBWriter(): port(0), send_timeout(0), publish_all(false), https(false), precision(0),
                          request_(nullptr)
        {
        }


        void setup() override;

        void loop() override;

        void dump_config() override;
#ifdef USE_BINARY_SENSOR
        void on_sensor_update(binary_sensor::BinarySensor* obj,
                              const std::string& measurement, const std::string& tags, const std::string& field_key,
                              bool state) const;
#endif
#ifdef USE_SENSOR
        void on_sensor_update(sensor::Sensor* obj, const std::string& measurement,
                              const std::string& tags, const std::string& field_key, float state) const;
#endif
#ifdef USE_TEXT_SENSOR
        void on_sensor_update(text_sensor::TextSensor* obj, const std::string& measurement,
                              const std::string& tags, const std::string& field_key, const std::string& state) const;
#endif

        void set_host(std::string host) { this->host = std::move(host); };
        void set_port(uint16_t port) { this->port = port; };

        void set_org_id(std::string org_id) { this->org_id = std::move(org_id); };
        void set_token(std::string token) { this->token = std::move(token); };
        void set_bucket(std::string bucket) { this->bucket = std::move(bucket); };
        void set_send_timeout(int timeout) { send_timeout = timeout; };
        void set_publish_all(bool all) { publish_all = all; };

        void add_setup_callback(const std::function<EntityBase *()>& fun)
        {
            setup_callbacks.push_back(fun);
        };
        void set_https(bool https) { this->https = https; };
        void set_precision(int precision) { this->precision = precision; };

    protected:
        void setup_client();
        static void escape_whitespace(std::string tags);
        void register_binary_sensor_callback(std::vector<EntityBase*> objs,
                                             binary_sensor::BinarySensor* binary_sensor) const;
        void register_sensor_callback(std::vector<EntityBase*> objs, sensor::Sensor* sensor) const;
        void register_text_sensor_callback(std::vector<EntityBase*> objs, text_sensor::TextSensor* text_sensor) const;
        void write(std::string& measurement, const std::string& tags, const std::string& field_key,
                   const std::string& value,
                   bool is_string) const;

        uint16_t port;
        std::string host;

        std::string org_id;
        std::string token;
        std::string bucket;
        std::string service_url;
        std::string field_key;

        int send_timeout;
        std::string tags;
        bool publish_all;
        bool https;
        int precision;

        std::vector<std::function<EntityBase *()>> setup_callbacks;

        http_request::HttpRequestComponent* request_;
    };
} // namespace influxdb
