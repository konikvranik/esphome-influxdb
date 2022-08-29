# InfluxDB2 custom component for ESPHome
Changes from Jepsson/esphome-influxdb:
- Changed to InfluxDB2 API

## Installation
Add this repository as an submodule in your esphome custom_compontents;
`git clone https://github.com/CalvinSchwartz/esphome-influxdb custom_components/influxdb2`

## Usage

Add `influxdb2` section to your ESPHome configuration file.

### Example configuration

```yaml
influxdb2:
  host: "influxdb-host"
  orgid: "influx_org"
  token: "token"
  bucket: "influx_bucket"
  sensors:
    meter_id:
      ignore: True
    ams_temperature:
      measurement: 'temperature'
      tags: 
        room: kitchen
```

### Configuration variables

* **host** (Required, string): Hostname or IP for the InfluxDB server
* **port** (Optional, int, default: 8086): Port number the InfluxDB server is listening on.
* **orgid** (Required, string): Organization ID when connecting to influxdb.
* **token** (Required, string): Token used when connecting to influxdb.
* **bucket** (Required, string): Name of influxdb bucket.
* **send_timeout** (Optional, time, default: "500ms"): Time to wait before sending UDP packets which have not been filled to max size.
* **publish_all** (Optional, boolean, default: True): If true, publish updates from all sensors unless explicitly ignored in per sensor configuration. If false, only publish updates from sensors explicitly configured.
* **tags** (Optional, mapping, default 'node: <esphome.name>'): Mapping of tag keys and values. 
* **sensors** (Optional, mapping, default: {}): Per sensor configuration. Keys are sensor IDs. All types of sensors are included in this mapping, there is no distinction between float, binary and text sensors.

#### Sensor configuration variables

* **ignore** (Optional, boolean, default: False): Whether or not to include updates for this sensor.
* **measurement** (Optional, string): Name of measurements with update from this sensor. Defaults to the sanitized name of the sensor.
* **tags** (Optional, mapping, default: {}): Additional tags added for this sensor.
