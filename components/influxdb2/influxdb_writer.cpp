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
    static const char* TAG = "influxdb2";


    void InfluxDBWriter::setup()
    {
        ESP_LOGCONFIG(TAG, "Setting up InfluxDB Writer...");

        setup_client();

        std::vector<EntityBase*> objs;
        for (const auto& fun : setup_callbacks)
            objs.push_back(fun());


        if (publish_all)
        {
#ifdef USE_BINARY_SENSOR
            for (auto* obj : App.get_binary_sensors())
            {
                register_binary_sensor_callback(objs, obj);
            }
#endif
#ifdef USE_SENSOR

            for (auto* obj : App.get_sensors())
            {
                register_sensor_callback(objs, obj);
            }
#endif
#ifdef USE_TEXT_SENSOR

            for (auto* obj : App.get_text_sensors())
            {
                register_text_sensor_callback(objs, obj);
            }
#endif
        }
    }

    void InfluxDBWriter::loop()
    {
    }

    void InfluxDBWriter::setup_client()
    {
        if (this->https)
        {
            this->service_url = "https";
        }
        else
        {
            this->service_url = "http";
        }
        this->service_url = this->service_url + "://" + this->host + ":" + to_string(this->port) +
            "/api/v2/write?org=" + this->org_id + "&bucket=" + this->bucket + "&precision=ns";

        this->request_->setup();

        this->request_->set_useragent("ESPHome InfluxDB Bot");
        this->request_->set_timeout(this->send_timeout);
    }

    std::string InfluxDBWriter::escape_whitespace(const std::string& tags)
    {
        std::string updated_tags = tags;
        for (size_t i = 0; i < updated_tags.length(); ++i)
        {
            if (updated_tags[i] == ' ')
            {
                updated_tags.insert(i, "\\");
                i++;
            }
        }
        return updated_tags;
    }

    std::string headers_to_string(const std::list<http_request::Header>& headers)
    {
        std::ostringstream oss;
        oss << "[";
        bool first = true;

        for (const auto& header : headers)
        {
            if (!first)
            {
                oss << ", ";
            }
            first = false;

            oss << "{" << header.name << ": " << header.value << "}";
        }

        oss << "]";
        return oss.str();
    }

    void InfluxDBWriter::write(std::string measurement,
                               const std::string& tags,
                               const std::string& field_key,
                               const std::string& value,
                               const bool is_string) const
    {
        std::replace(measurement.begin(), measurement.end(), '-', '_');

        std::list<http_request::Header> headers;
        http_request::Header header;
        header.name = "Content-Type";
        header.value = "text/plain";
        headers.push_back(header);
        if ((!this->token.empty()))
        {
            header.name = "Authorization";
            header.value = this->token;
            headers.push_back(header);
        }

        std::string body = measurement + tags + " " + field_key + "=" + (is_string ? ("\"" + value + "\"") : value);

        ESP_LOGV(TAG, "Measurement: %s", measurement.c_str());
        ESP_LOGV(TAG, "Tags: %s", tags.c_str());
        ESP_LOGV(TAG, "Field key: %s", field_key.c_str());
        ESP_LOGV(TAG, "Value: %s", value.c_str());

        ESP_LOGD(TAG, "InfluxDB URL: %s", this->service_url.c_str());
        ESP_LOGD(TAG, "InfluxDB headers: %s", headers_to_string(headers).c_str());
        ESP_LOGD(TAG, "InfluxDB body: %s", body.c_str());
        ESP_LOGV(TAG, "Body size: %lu", body.size());
        ESP_LOGV(TAG, "Header count: %lu", headers.size());


        if (this->request_ == nullptr)
        {
            ESP_LOGE(TAG, "Client is nullptr");
            return;
        }
        std::shared_ptr<http_request::HttpContainer> response = this->request_->post(
            this->service_url, body, headers);

        if (response == nullptr)
        {
            ESP_LOGE(TAG, "Response is nullptr, request failed.");
            return;
        }

        if (response->status_code < 300)
        {
            ESP_LOGD(TAG, "Response satus: %d", response->status_code);
        }
        else
        {
            uint8_t buf[64];
            std::string response_body;

            int bytes_read;
            while ((bytes_read = response->read(buf, sizeof(buf))) > 0)
            {
                response_body.append(reinterpret_cast<const char*>(buf), bytes_read);
            }

            ESP_LOGE(TAG, "Failed! HTTP Status %d: %s", response->status_code, response_body.c_str());
        }
        response->end();
    }

    bool sensor_precondition(std::vector<EntityBase*> objs, EntityBase* sensor)
    {
        return !sensor->is_internal()
            &&
            std::none_of(objs.begin(), objs.end(), [&sensor](const EntityBase* o) { return o == sensor; });
    }

#ifdef USE_BINARY_SENSOR
    void InfluxDBWriter::register_binary_sensor_callback(std::vector<EntityBase*> objs,
                                                         binary_sensor::BinarySensor* binary_sensor) const
    {
        if (
            sensor_precondition(std::move(objs), binary_sensor)
        )
        {
            binary_sensor->add_on_state_callback([this, binary_sensor](bool state)
            {
                this->on_sensor_update(binary_sensor, binary_sensor->get_object_id(), this->tags, this->field_key,
                                       state);
            });
        }
    }
#endif

#ifdef USE_SENSOR
    void InfluxDBWriter::register_sensor_callback(std::vector<EntityBase*> objs, sensor::Sensor* sensor) const
    {
        if (
            sensor_precondition(std::move(objs), sensor)
        )
        {
            sensor->add_on_state_callback([this, sensor](float state)
            {
                this->on_sensor_update(sensor, sensor->get_object_id(), this->tags, this->field_key, state);
            });
        }
    }
#endif

#ifdef  USE_TEXT_SENSOR
    void InfluxDBWriter::register_text_sensor_callback(std::vector<EntityBase*> objs,
                                                       text_sensor::TextSensor* text_sensor) const
    {
        if (
            sensor_precondition(std::move(objs), text_sensor)
        )
        {
            text_sensor->add_on_state_callback([this, text_sensor](const std::string& state)
            {
                this->on_sensor_update(text_sensor, text_sensor->get_object_id(), this->tags, this->field_key, state);
            });
        }
    }
#endif

    void InfluxDBWriter::dump_config()
    {
        ESP_LOGCONFIG(TAG, "InfluxDB Writer:");
        ESP_LOGCONFIG(TAG, "  Address: %s:%u", host.c_str(), port);
        ESP_LOGCONFIG(TAG, "  Bucket: %s", bucket.c_str());
    }

#ifdef USE_BINARY_SENSOR
    void InfluxDBWriter::on_sensor_update(binary_sensor::BinarySensor* obj, const std::string& measurement,
                                          const std::string& tags, const std::string& field_key, bool state) const
    {
        ESP_LOGD(TAG, "Updating binary sensor: %s", field_key.c_str());
        write(measurement, tags, field_key, state ? "t" : "f", false);
    }
#endif

#ifdef USE_SENSOR
    void InfluxDBWriter::on_sensor_update(sensor::Sensor* obj, const std::string& measurement, const std::string& tags,
                                          const std::string& field_key, float state) const
    {
#ifdef USE_ESP_IDF
        if (!std::isnan(state))
#else
        if (!isnan(state))
#endif
        {
            std::stringstream value;
            value << std::fixed << std::setprecision(this->precision) << state;
            ESP_LOGD(TAG, "Updating sensor: %s", field_key.c_str());
            write(measurement, tags, field_key, value.str(), false);
        }
    }
#endif

#ifdef USE_TEXT_SENSOR
    void InfluxDBWriter::on_sensor_update(text_sensor::TextSensor* obj, const std::string& measurement,
                                          const std::string& tags, const std::string& field_key,
                                          const std::string& state) const
    {
        ESP_LOGD(TAG, "Updating text sensor: %s", field_key.c_str());
        write(measurement, tags, field_key, state, true);
    }
#endif
} // namespace influxdb
