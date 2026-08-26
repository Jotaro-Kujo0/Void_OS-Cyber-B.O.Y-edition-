// hal_mqtt.h — MQTT publisher (SKELETON).
//
// Multi-device engagements: one operator watches a broker that all
// devices publish to. Lets a SOC stay aware of which devices are
// capturing what.

#pragma once
#include <stdint.h>
#include <stdbool.h>

void hal_mqtt_init();
bool hal_mqtt_connect(const char *broker, uint16_t port);
bool hal_mqtt_publish(const char *topic, const char *payload);
void hal_mqtt_disconnect();
