#include "mqtt.h"
#include <mosquitto.h>
#include <stdio.h>
#define MQTT_HOSTNAME "192.168.2.101"
#define MQTT_PORT 1883
#define MQTT_KEEP_ALIVE 60
#define MQTT_PASSWORD "1761856778"
#define MQTT_USERNAME "george"
#define MQTT_TOPIC "spectrum_sense"

/* test with
mosquitto_sub -h 192.168.2.101 -u george -P 1761856778 -t "topic"
mosquitto_pub -h 192.168.2.101 -u george -P 1761856778 -m "ON" -t "topic"
*/
struct mosquitto *mosq;


/* Callback called when the client receives a CONNACK message from the broker. */
void on_connect(struct mosquitto *mosq, void *obj, int reason_code)
{
	/* Print out the connection result. mosquitto_connack_string() produces an
	 * appropriate string for MQTT v3.x clients, the equivalent for MQTT v5.0
	 * clients is mosquitto_reason_string().
	 */
	printf("on_connect: %s\n", mosquitto_connack_string(reason_code));
	if(reason_code != 0){
		/* If the connection fails for any reason, we don't want to keep on
		 * retrying in this example, so disconnect. Without this, the client
		 * will attempt to reconnect. */
		mosquitto_disconnect(mosq);
	}

	/* You may wish to set a flag here to indicate to your application that the
	 * client is now connected. */
}

/* Callback called when the client knows to the best of its abilities that a
 * PUBLISH has been successfully sent. For QoS 0 this means the message has
 * been completely written to the operating system. For QoS 1 this means we
 * have received a PUBACK from the broker. For QoS 2 this means we have
 * received a PUBCOMP from the broker. */
void on_publish(struct mosquitto *mosq, void *obj, int mid) {
	printf("Message with mid %d has been published.\n", mid);
}

void publish_message(char *message, int size) {
	int rc;

	rc = mosquitto_publish(mosq, NULL, MQTT_TOPIC, size, message, 2, false);
	if(rc != MOSQ_ERR_SUCCESS){
		fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(rc));
	}

}

int mqtt_setup() {
	mosquitto_lib_init();
	int rc;
	
	mosq = mosquitto_new(NULL, true, NULL);
	if(mosq == NULL){
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}
	
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_publish_callback_set(mosq, on_publish);
	
	mosquitto_username_pw_set(mosq, MQTT_USERNAME, MQTT_PASSWORD);
	rc = mosquitto_connect(mosq, MQTT_HOSTNAME, MQTT_PORT, MQTT_KEEP_ALIVE);
	if(rc != MOSQ_ERR_SUCCESS){
		mosquitto_destroy(mosq);
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
		return 1;
	}
	
	rc = mosquitto_loop_start(mosq);
	if(rc != MOSQ_ERR_SUCCESS){
		mosquitto_destroy(mosq);
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
		return 1;
	}
	
	//publish_message("test_message", 12);
	
	return 0;
}

void mqtt_cleanup() {
	mosquitto_lib_cleanup();
}
