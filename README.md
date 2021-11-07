# SmartBlinds
 SmartBlinds for Home Assistant
 

 
# Battery Sensor
 
 sensor:
  - platform: mqtt
    state_topic: "SmartBlinds/Blind-1/Battery"
    name: Blind_1_Battery
    unit_of_measurement: "%"
    icon: mdi:Battery-80


# Vertical Stack Card Configuration

type: vertical-stack
cards:
  - type: picture
    image: /local/externaldir/customicons/Blinds.jpg
    tap_action:
      action: none
    hold_action:
      action: none
  - type: glance
    entities:
      - entity: sun.sun
        name: OPEN
        tap_action:
          navigation_path: none
          url_path: none
          action: call-service
          service: mqtt.publish
          service_data:
            topic: SmartBlinds/Blind-1/Angle
            payload: 0
            retain: true
      - entity: sun.sun
        name: 45°
        tap_action:
          navigation_path: none
          url_path: none
          action: call-service
          service: mqtt.publish
          service_data:
            topic: SmartBlinds/Blind-1/Angle
            payload: 90
            retain: true
      - entity: sun.sun
        name: CLOSE
        tap_action:
          navigation_path: none
          url_path: none
          action: call-service
          service: mqtt.publish
          service_data:
            topic: SmartBlinds/Blind-1/Angle
            payload: 180
            retain: true
    show_icon: false
    show_state: false
    show_name: true
  - type: custom:mini-graph-card
    entities:
      - sensor.blind_1_battery
    hours_to_show: 24
    points_per_hour: 6
    show:
      extrema: true

