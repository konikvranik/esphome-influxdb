#include "influxdb_writer.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <string>
#include "esphome/core/helpers.h"
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
#ifdef USE_SWITCH
            for (auto* obj : App.get_switches())
            {
                register_switch_callback(objs, obj);
            }
#endif
#ifdef USE_LIGHT
            for (auto* obj : App.get_lights())
            {
                register_light_callback(objs, obj);
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
        ESP_LOGCONFIG(TAG, "InfluxDB client configured for %s:%u", host.c_str(), port);
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

    void InfluxDBWriter::write(std::string measurement,
                               const std::string& tags,
                               const std::string& field_key,
                               const std::string& value,
                               const bool is_string) const
    {
        std::replace(measurement.begin(), measurement.end(), '-', '_');

        std::string body = measurement + tags + " " + field_key + "=" + (is_string ? ("\"" + value + "\"") : value);

        ESP_LOGV(TAG, "Measurement: %s", measurement.c_str());
        ESP_LOGV(TAG, "Tags: %s", tags.c_str());
        ESP_LOGV(TAG, "Field key: %s", field_key.c_str());
        ESP_LOGV(TAG, "Value: %s", value.c_str());
        ESP_LOGD(TAG, "InfluxDB body: %s", body.c_str());

        // Build URL path on stack
        char url_path[256];
        snprintf(url_path, sizeof(url_path),
                 "/api/v2/write?org=%s&bucket=%s&precision=ns",
                 org_id.c_str(), bucket.c_str());

        WiFiClient client;
        client.setTimeout(this->send_timeout);

        ESP_LOGD(TAG, "Connecting to %s:%u", host.c_str(), port);

        if (!client.connect(host.c_str(), port))
        {
            ESP_LOGE(TAG, "Connection failed to %s:%u", host.c_str(), port);
            return;
        }

        // Send HTTP request
        client.printf("POST %s HTTP/1.1\r\n", url_path);
        client.printf("Host: %s:%u\r\n", host.c_str(), port);
        client.print(F("Content-Type: text/plain\r\n"));
        if (!token.empty())
        {
            client.printf("Authorization: %s\r\n", token.c_str());
        }
        client.printf("Content-Length: %u\r\n", (unsigned)body.length());
        client.print(F("Connection: close\r\n"));
        client.print(F("User-Agent: ESPHome InfluxDB\r\n"));
        client.print(F("\r\n"));
        client.print(body.c_str());

        // Read status line
        unsigned long deadline = millis() + this->send_timeout;
        while (client.available() == 0)
        {
            if (millis() > deadline)
            {
                ESP_LOGE(TAG, "HTTP response timeout");
                client.stop();
                return;
            }
            yield();
        }

        // Parse status code from first line: "HTTP/1.1 204 No Content"
        String status_line = client.readStringUntil('\n');
        int status_code = 0;
        int space_pos = status_line.indexOf(' ');
        if (space_pos >= 0)
        {
            status_code = status_line.substring(space_pos + 1, space_pos + 4).toInt();
        }

        if (status_code < 300)
        {
            ESP_LOGD(TAG, "Response status: %d", status_code);
        }
        else
        {
            // Read remaining response for error logging
            String response_body;
            while (client.available())
            {
                response_body += client.readStringUntil('\n');
            }
            ESP_LOGE(TAG, "Failed! HTTP Status %d: %s", status_code, response_body.c_str());
        }

        client.stop();
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
                char buf[64];
                auto sr = binary_sensor->get_object_id_to(buf);
                this->on_binary_sensor_update(binary_sensor, sr.str(), this->tags,
                                              this->field_key,
                                              state);
            });
        }
    }
#endif

#ifdef USE_SWITCH
    void InfluxDBWriter::register_switch_callback(std::vector<EntityBase*> objs, switch_::Switch* s) const
    {
        if (
            sensor_precondition(std::move(objs), s)
        )
        {
            s->add_on_state_callback([this, s](bool state)
            {
                char buf[64];
                auto sr = s->get_object_id_to(buf);
                this->on_switch_update(s, sr.str(), this->tags, this->field_key, state);
            });
        }
    }
#endif

#ifdef USE_LIGHT
    namespace {
    struct LightUpdateListener : public light::LightTargetStateReachedListener {
        LightUpdateListener(const InfluxDBWriter* writer, light::LightState* light,
                            std::string tags, std::string field_key)
            : writer_(writer), light_(light),
              tags_(std::move(tags)), field_key_(std::move(field_key)) {}

        void on_light_target_state_reached() override {
            char buf[64];
            auto sr = light_->get_object_id_to(buf);
            writer_->on_light_update(light_, sr.str(),
                                     tags_, field_key_);
        }

    private:
        const InfluxDBWriter* writer_;
        light::LightState* light_;
        std::string tags_;
        std::string field_key_;
    };
    } // anonymous namespace

    void InfluxDBWriter::register_light_callback(std::vector<EntityBase*> objs, light::LightState* light) const
    {
        if (
            sensor_precondition(std::move(objs), light)
        )
        {
            auto* listener = new LightUpdateListener(this, light, this->tags, this->field_key);
            light->add_target_state_reached_listener(listener);
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
                char buf[64];
                auto sr = sensor->get_object_id_to(buf);
                this->on_sensor_update(sensor, sr.str(), this->tags, this->field_key, state);
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
                char buf[64];
                auto sr = text_sensor->get_object_id_to(buf);
                this->on_sensor_update(text_sensor, sr.str(), this->tags, this->field_key, state);
            });
        }
    }
#endif

    // Overloaded helpers for type-aware codegen
#ifdef USE_SENSOR
    void InfluxDBWriter::influxdb_register_entity(sensor::Sensor* entity, const std::string& measurement,
                                                   const std::string& tags, const std::string& field_key, float state) {
        this->on_sensor_update(entity, measurement, tags, field_key, state);
    }
#endif
#ifdef USE_BINARY_SENSOR
    void InfluxDBWriter::influxdb_register_entity(binary_sensor::BinarySensor* entity, const std::string& measurement,
                                                   const std::string& tags, const std::string& field_key, bool state) {
        this->on_binary_sensor_update(entity, measurement, tags, field_key, state);
    }
#endif
#ifdef USE_SWITCH
    void InfluxDBWriter::influxdb_register_entity(switch_::Switch* entity, const std::string& measurement,
                                                   const std::string& tags, const std::string& field_key, bool state) {
        this->on_switch_update(entity, measurement, tags, field_key, state);
    }
#endif
#ifdef USE_TEXT_SENSOR
    void InfluxDBWriter::influxdb_register_entity(text_sensor::TextSensor* entity, const std::string& measurement,
                                                   const std::string& tags, const std::string& field_key, const std::string& state) {
        this->on_sensor_update(entity, measurement, tags, field_key, state);
    }
#endif

    void InfluxDBWriter::dump_config()
    {
        ESP_LOGCONFIG(TAG, "InfluxDB Writer:");
        ESP_LOGCONFIG(TAG, "  Address: %s:%u", host.c_str(), port);
        ESP_LOGCONFIG(TAG, "  Bucket: %s", bucket.c_str());
    }

#ifdef USE_BINARY_SENSOR
    void InfluxDBWriter::on_binary_sensor_update(binary_sensor::BinarySensor* obj, const std::string& measurement,
                                                 const std::string& tags, const std::string& field_key,
                                                 bool state) const
    {
        ESP_LOGD(TAG, "Updating binary sensor: %s", field_key.c_str());
        write(measurement, update_tags(obj, tags), field_key, state ? "t" : "f", false);
    }
#endif

#ifdef USE_SWITCH
    void InfluxDBWriter::on_switch_update(switch_::Switch* obj, const std::string& measurement, const std::string& tags,
                                          const std::string& field_key, bool state) const
    {
        ESP_LOGD(TAG, "Updating switch: %s", field_key.c_str());
        write(measurement, update_tags(obj, tags), field_key, state ? "t" : "f", false);
    }
#endif

#ifdef USE_LIGHT
    void InfluxDBWriter::on_light_update(light::LightState* obj, const std::string& measurement,
                                         const std::string& tags,
                                         const std::string& field_key) const
    {
        float light_val = 0.0f;
        bool state;
        obj->current_values_as_binary(&state);
        if (state)
        {
            float brightness;
            obj->current_values_as_brightness(&brightness);
#ifdef USE_ESP_IDF
            if (!std::isnan(brightness))
#else
            if (!isnan(brightness))
#endif
            {
                light_val = brightness;
            }
            else
            {
                light_val = 1.0f;
            }
        }
        char val_buf[VALUE_ACCURACY_MAX_LEN];
        size_t len = value_accuracy_to_buf(val_buf, light_val, this->precision);
        ESP_LOGD(TAG, "Updating light: %s", field_key.c_str());
        write(measurement, update_tags(obj, tags), field_key, std::string(val_buf, len), false);
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
            char val_buf[VALUE_ACCURACY_MAX_LEN];
            size_t len = value_accuracy_to_buf(val_buf, state, this->precision);
            ESP_LOGD(TAG, "Updating sensor: %s", field_key.c_str());
            write(measurement, update_tags(obj, tags), field_key, std::string(val_buf, len), false);
        }
    }
#endif

#ifdef USE_TEXT_SENSOR

    void InfluxDBWriter::on_sensor_update(text_sensor::TextSensor* obj, const std::string& measurement,
                                          const std::string& tags, const std::string& field_key,
                                          const std::string& state) const
    {
        ESP_LOGD(TAG, "Updating text sensor: %s", field_key.c_str());
        write(measurement, update_tags(obj, tags), field_key, state, true);
    }


#endif
    std::string InfluxDBWriter::update_tags(const EntityBase* obj, const std::string& tags)
    {
        if (tags.empty())
        {
            if (obj->get_name().empty())
            {
                return "";
            }
            return "friendly_name=" + escape_whitespace(obj->get_name());
        }
        if (obj->get_name().empty() || tags.find("friendly_name=") != std::string::npos)
        {
            return tags;
        }
        return tags + ",friendly_name=" + escape_whitespace(obj->get_name());
    }
} // namespace influxdb
