#ifndef MQTT_H
#define MQTT_H

void publish_station_info(double center_freq, double sample_rate);
void publish_message(char *message, int size, int embed_station_id);
int mqtt_setup();
int mqtt_setup_receiver();
void mqtt_cleanup();

#endif
