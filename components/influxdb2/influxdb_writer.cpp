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

#ifdef USE_ESP_IDF
        this->request_ = new http_request::HttpRequestIDF();
#else
            this->request_ = new http_request::HttpRequestArduino();
#endif
        this->request_->setup();

        this->request_->set_useragent("ESPHome InfluxDB Bot");
        this->request_->set_timeout(this->send_timeout);
    }

    void InfluxDBWriter::escape_whitespace(std::string tags)
    {
        for (size_t i = 0; i < tags.length(); ++i)
        {
            // Add the escape char "\" to all whitespaces in the tags with an "\ "
            if (tags[i] == ' ')
            {
                tags.insert(i, "\\");
                i++; // Skip the inserted backslash
            }
        }
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
        escape_whitespace(tags);

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

        ESP_LOGD(TAG, "InfluxDB URL: %s", this->service_url.c_str());
        ESP_LOGD(TAG, "InfluxDB headers: %s", headers_to_string(headers).c_str());
        ESP_LOGD(TAG, "InfluxDB packet: %s", body.c_str());
        ESP_LOGD("http_request", "Body size: %lu", body.size());
        ESP_LOGD("http_request", "Header count: %lu", headers.size());

        constexpr esp_task_wdt_config_t cfg = {
            .timeout_ms = 500,
            .idle_core_mask = 0,
            .trigger_panic = false
        };

        esp_task_wdt_init(&cfg); // Timeout 10 sekund
        esp_task_wdt_add(nullptr); // Přidání aktuální úlohy k watchdogu

        std::shared_ptr<http_request::HttpContainer> response = this->request_->post(this->service_url, body, headers);

        if (response->status_code != 200)
        {
            ESP_LOGE("http_request", "Failed! HTTP Status: %d", response->status_code);
        }
        else
        {
            uint8_t buf[64]; // Alokujeme statický buffer pro data
            std::string response_body; // Řetězec pro uložení celé odpovědi (postupně načítán)

            // Načítáme ze streamu až do konce
            int bytes_read;
            while ((bytes_read = response->read(buf, sizeof(buf))) > 0)
            {
                response_body.append(reinterpret_cast<const char*>(buf), bytes_read);
            }

            // Zalogujeme výstup (musí být ukončen null-terminátorem)
            ESP_LOGD("http_request", "Response: %s", response_body.c_str());
        }
        response->end();
        esp_task_wdt_delete(nullptr);
        delete &body;
    }

    bool sensor_precondition(std::vector<EntityBase*> objs, EntityBase* sensor)
    {
        return !sensor->is_internal()
            &&
            std::none_of(objs.begin(), objs.end(), [&sensor](const EntityBase* o) { return o == sensor; });
    }

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
