#include "influxdb_writer.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <string>
#include <iomanip>
#include <sstream>
#include <utility>

#ifdef USE_LOGGER
#include "esphome/components/logger/logger.h"
#endif


namespace esphome::influxdb2
{
    static const char* TAG = "influxdb_jab";

    void InfluxDBWriter::setup()
    {
        ESP_LOGCONFIG(TAG, "Setting up InfluxDB Writer...");
        std::vector<EntityBase*> objs;
        for (const auto& fun : setup_callbacks)
            objs.push_back(fun());

        if (this->https)
        {
            this->service_url = "https";
        }
        else
        {
            this->service_url = "http";
        }
        this->service_url = this->service_url + "://" + this->host + ":" + to_string(this->port) +
            "/api/v2/write?org=" + this->orgid + "&bucket=" + this->bucket + "&precision=ns";

#ifdef USE_ESP_IDF
        this->request_ = new http_request::HttpRequestIDF();
#else
            this->request_ = new http_request::HttpRequestArduino();
#endif
        this->request_->setup();

        this->request_->set_useragent("ESPHome InfluxDB Bot");
        this->request_->set_timeout(this->send_timeout);

        if (publish_all)
        {
#ifdef USE_BINARY_SENSOR
            for (auto* obj : App.get_binary_sensors())
            {
                if (
                    !obj->is_internal()
                    &&
                    std::none_of(objs.begin(), objs.end(), [&obj](const EntityBase* o) { return o == obj; })
                )
                {
                    obj->add_on_state_callback([this, obj](bool state)
                    {
                        this->on_sensor_update(obj, obj->get_object_id(), tags, field_key, state);
                    });
                }
            }
#endif
#ifdef USE_SENSOR
            for (auto* obj : App.get_sensors())
            {
                if (
                    !obj->is_internal()
                    &&
                    std::none_of(objs.begin(), objs.end(), [&obj](const EntityBase* o) { return o == obj; })
                )
                {
                    obj->add_on_state_callback([this, obj](float state)
                    {
                        this->on_sensor_update(obj, obj->get_object_id(), tags, field_key, state);
                    });
                }
            }
#endif
#ifdef USE_TEXT_SENSOR
            for (auto* obj : App.get_text_sensors())
            {
                if (
                    !obj->is_internal()
                    &&
                    std::none_of(objs.begin(), objs.end(), [&obj](const EntityBase* o) { return o == obj; })
                )
                {
                    obj->add_on_state_callback([this, obj](const std::string& state)
                    {
                        this->on_sensor_update(obj, obj->get_object_id(), tags, field_key, state);
                    });
                }
            }
#endif
        }
    }

    void InfluxDBWriter::loop()
    {
    }

    void InfluxDBWriter::write(std::string measurement,
                               std::string tags,
                               const std::string& field_key,
                               const std::string& value,
                               const bool is_string) const
    {
        std::replace(measurement.begin(), measurement.end(), '-', '_');
        for (size_t i = 0; i < tags.length(); ++i)
        {
            // Add the escape char "\" to all whitespaces in the tags with an "\ "
            if (tags[i] == ' ')
            {
                tags.insert(i, "\\");
                i++; // Skip the inserted backslash
            }
        }
        std::string line =
            measurement + tags + " " + field_key + "=" + (is_string ? ("\"" + value + "\"") : value);

        std::list<http_request::Header> headers;
        http_request::Header header;
        header.name = "Content-Type";
        header.value = "text/plain";
        headers.push_back(header);
        if ((!this->orgid.empty()))
        {
            header.name = "Authorization";
            header.value = this->token;
            headers.push_back(header);
        }

        this->request_->post(this->service_url, line, headers);

        ESP_LOGD(TAG, "InfluxDB packet: %s", line.c_str());
    }

    void InfluxDBWriter::dump_config()
    {
        ESP_LOGCONFIG(TAG, "InfluxDB Writer:");
        ESP_LOGCONFIG(TAG, "  Address: %s:%u", host.c_str(), port);
        ESP_LOGCONFIG(TAG, "  Bucket: %s", bucket.c_str());
    }

#ifdef USE_BINARY_SENSOR
    void InfluxDBWriter::on_sensor_update(binary_sensor::BinarySensor* obj,
                                          std::string measurement, std::string tags, const std::string& field_key,
                                          bool state) const
    {
        write(std::move(measurement), std::move(tags), field_key, state ? "t" : "f", false);
    }
#endif

#ifdef USE_SENSOR
    void InfluxDBWriter::on_sensor_update(sensor::Sensor* obj,
                                          std::string measurement, std::string tags, const std::string& field_key,
                                          float state) const
    {
#ifdef USE_ESP_IDF
        if (!std::isnan(state))
        {
#else
            if (!isnan(state)) {
#endif

            std::stringstream value;
            value << std::fixed << std::setprecision(this->precision) << state;
            write(std::move(measurement), std::move(tags), field_key, value.str(), false);
        }
    }
#endif

#ifdef USE_TEXT_SENSOR
    void InfluxDBWriter::on_sensor_update(text_sensor::TextSensor* obj,
                                          std::string measurement, std::string tags, const std::string& field_key,
                                          const std::string& state) const
    {
        write(std::move(measurement), std::move(tags), field_key, state, true);
    }
#endif
} // namespace influxdb
