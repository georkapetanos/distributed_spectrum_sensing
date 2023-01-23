#include "mqtt.h"
#include <mosquitto.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sqlite3.h> 
#define MQTT_TOPIC "spectrum_sense"
#define CONFIGURATION_FILE "./config.yaml"
#define DB_FILE "./dss.db"
#define IMPORT_LINE_SIZE 256
#define MAX_STATIONS 16

typedef struct config_data{
	char hostname[256];
	unsigned short port;
	unsigned short keep_alive;
	char username[64];
	char password[64];
	char id[16];
	char location[64];
} config_dataT;

typedef struct stations{
	char id[9];
	double center_freq;
} stationsT;

struct mosquitto *mosq;
config_dataT parsed_data;
stationsT *stations_list;
int number_of_stations;
sqlite3 *db;
int sql_id;

void insert_station(char *station_id, double center_freq) {
	int i;

	if(stations_list == NULL) {
		stations_list = (stationsT *) malloc(MAX_STATIONS * sizeof(stations_list));
	}
	for(i = 0; i < number_of_stations; i++) {
		if(strncmp(stations_list[i].id, station_id, 8) == 0) {
			stations_list[i].center_freq = center_freq;
			return ;
		}
	}
	strcpy(stations_list[number_of_stations].id, station_id);
	stations_list[number_of_stations].center_freq = center_freq;
	number_of_stations++;
}

double return_station(char *station_id) {
	int i;
	
	for(i = 0; i < number_of_stations; i++) {
		if(strncmp(stations_list[i].id, station_id, 8) == 0) {
			return stations_list[i].center_freq;
		}
	}
	
	return -1;
}

double check_station(char *station_id, double center_freq) {
	int i;
	
	for(i = 0; i < number_of_stations; i++) {
		if((strncmp(stations_list[i].id, station_id, 8) == 0) && (stations_list[i].center_freq == center_freq)) {
			return stations_list[i].center_freq;
		}
	}
	
	return -1;
}

void update_station() {

}

static int callback(void *NotUsed, int argc, char **argv, char **azColName) {
   int i;
   for(i = 0; i<argc; i++) {
      printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   printf("\n");
   return 0;
}

void parse_configuration_file(config_dataT *parsed_data) {
	FILE *fp;
	char line[256];
	char *chr;
	
	fp = fopen(CONFIGURATION_FILE, "r");
	if(fp == NULL) {
		printf("Error opening configuration file %s.\n", CONFIGURATION_FILE);
		exit(1);
	}
	
	while(1) {
		if(fgets(line, IMPORT_LINE_SIZE, fp) == NULL) { //read file line by line
			break;
		}
	
		if(line[0] == '#') { //ignore lines starting with '#'
			continue;
		}
		
		//remove new line character
		chr = strchr(line, '\n');
		*chr = '\0';
		
		chr = strchr(line, ':');
		if(chr == NULL) {
			continue;
		}

		if(!strncmp(line, "mqtt_hostname", 13)) {
			strcpy(parsed_data->hostname, chr+2);
		} else if(!strncmp(line, "mqtt_port", 9)) {
			parsed_data->port = atoi(chr+2);
		} else if(!strncmp(line, "mqtt_keep_alive", 15)) {
			parsed_data->keep_alive = atoi(chr+2);
		} else if(!strncmp(line, "mqtt_password", 13)) {
			strcpy(parsed_data->password, chr+2);
		} else if(!strncmp(line, "mqtt_username", 13)) {
			strcpy(parsed_data->username, chr+2);
		} else if(!strncmp(line, "station_id", 10)) {
			strcpy(parsed_data->id, chr+2);
		} else if(!strncmp(line, "station_location", 16)) {
			strcpy(parsed_data->location, chr+2);
		}
	}
	
	fclose(fp);
}

void publish_station_info(double center_freq, double sample_rate) {
	char message[256];

	strcpy(message, "INFO,");
	strcat(message, parsed_data.id);
	strcat(message, ",");
	strcat(message, parsed_data.location);
	strcat(message, ",");
	sprintf(&message[strlen(message)], "%10.3e,%10.3e", center_freq, sample_rate);

	publish_message(message, strlen(message), 0);
}

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

void on_connect_receiver(struct mosquitto *mosq, void *obj, int reason_code)
{
	int rc;
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

	/* Making subscriptions in the on_connect() callback means that if the
	 * connection drops and is automatically resumed by the client, then the
	 * subscriptions will be recreated when the client reconnects. */
	rc = mosquitto_subscribe(mosq, NULL, MQTT_TOPIC, 1);
	if(rc != MOSQ_ERR_SUCCESS){
		fprintf(stderr, "Error subscribing: %s\n", mosquitto_strerror(rc));
		/* We might as well disconnect if we were unable to subscribe */
		mosquitto_disconnect(mosq);
	}
}

/* Callback called when the client knows to the best of its abilities that a
 * PUBLISH has been successfully sent. For QoS 0 this means the message has
 * been completely written to the operating system. For QoS 1 this means we
 * have received a PUBACK from the broker. For QoS 2 this means we have
 * received a PUBCOMP from the broker. */
void on_publish(struct mosquitto *mosq, void *obj, int mid) {
	printf("Message with mid %d has been published.\n", mid);
}

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg) {
	/* This blindly prints the payload, but the payload can be anything so take care. */
	char *message = NULL, *token = NULL;
	char id[9];
	char sql_statement[256], temp_sql[16];
	double coord_x, coord_y, center_freq = 0, sample_rate = 0;
	double left_bound, right_bound, dB, bandwidth;
	char *zErrMsg = 0;
	int rc;
	
	sqlite3_open(DB_FILE, &db);
	
	message = strdup((char *)msg->payload);
	token = strtok(message, ",");
	
	if(strncmp(token, "INFO", 4) == 0) { //station info message
		token = strtok(NULL, ",");
		strcpy(id, token);
		token = strtok(NULL, ",");
		coord_x = atof(token);
		token = strtok(NULL, ",");
		coord_y = atof(token);
		token = strtok(NULL, ",");
		center_freq = atof(token);
		token = strtok(NULL, ",");
		sample_rate = atof(token);
		printf("\n\x1B[32mStation_id=%s, x=%f, y=%f, center_frequency=%.2f Hz, sample_rate=%.2f Hz\n", id, coord_x, coord_y, center_freq, sample_rate);
		if(check_station(id, center_freq) == -1) { //check if station has changed center frequency
			insert_station(id, center_freq);
		}
		printf("Number of stations = %d\x1B[0m\n", number_of_stations);
	} else {
		strcpy(id, token);
		center_freq = return_station(token);
		token = strtok(NULL, ",");
		left_bound = atof(token);
		token = strtok(NULL, ",");
		right_bound = atof(token);
		token = strtok(NULL, ",");
		dB = atof(token);
		bandwidth = abs(left_bound - right_bound);
		printf("\nRx @ %.2f Hz, Bandwidth = %.2f Hz, %.2f dB from %s\n", center_freq + left_bound + bandwidth / 2, bandwidth, dB, id);
		strcpy(sql_statement, "INSERT INTO DETECTIONS (ID,IDSTATION,FREQUENCY,BANDWIDTH,DB) VALUES (");
		sprintf(temp_sql, "%d, \'", sql_id);
		strcat(sql_statement, temp_sql);
		sprintf(temp_sql, "%s\', ", id);
		strcat(sql_statement, temp_sql);
		sprintf(temp_sql, "%.2f, ", center_freq + left_bound + bandwidth / 2);
		strcat(sql_statement, temp_sql);
		sprintf(temp_sql, "%.2f, ", bandwidth);
		strcat(sql_statement, temp_sql);
		sprintf(temp_sql, "%.2f);", dB);
		strcat(sql_statement, temp_sql);
		printf("%s\n", sql_statement);
		/* Execute SQL statement */
		rc = sqlite3_exec(db, sql_statement, callback, 0, &zErrMsg);
	   
		if( rc != SQLITE_OK ){
			fprintf(stderr, "SQL error: %s\n", zErrMsg);
			sqlite3_free(zErrMsg);
		} else {
			fprintf(stdout, "Records created successfully.\n");
		}
		sql_id++;
	}
	
	sqlite3_close(db);
	free(message);
}

void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	int i;
	bool have_subscription = false;

	/* In this example we only subscribe to a single topic at once, but a
	 * SUBSCRIBE can contain many topics at once, so this is one way to check
	 * them all. */
	for(i=0; i<qos_count; i++){
		printf("on_subscribe: %d:granted qos = %d\n", i, granted_qos[i]);
		if(granted_qos[i] <= 2){
			have_subscription = true;
		}
	}
	if(have_subscription == false){
		/* The broker rejected all of our subscriptions, we know we only sent
		 * the one SUBSCRIBE, so there is no point remaining connected. */
		fprintf(stderr, "Error: All subscriptions rejected.\n");
		mosquitto_disconnect(mosq);
	}
}

/*
	int embed_station_id = 0 transmit message as is
	int embed_station_id = 1 embed station id before message
*/
void publish_message(char *message, int size, int embed_station_id) {
	int rc;
	char message_with_id[1024];
	
	if(embed_station_id) {
 		strcpy(message_with_id, parsed_data.id);
		strcat(message_with_id, ",");
		strncat(message_with_id, message, size);
		rc = mosquitto_publish(mosq, NULL, MQTT_TOPIC, size + 9, message_with_id, 2, false);
	} else {
		rc = mosquitto_publish(mosq, NULL, MQTT_TOPIC, size, message, 2, false);
	}
	
	if(rc != MOSQ_ERR_SUCCESS){
		fprintf(stderr, "Error publishing: %s\n", mosquitto_strerror(rc));
	}

}

int mqtt_setup_receiver() {
	int rc;
	char *sql;
	char *zErrMsg = 0;

	parse_configuration_file(&parsed_data);
	mosquitto_lib_init();

	stations_list = NULL;
	number_of_stations = 0;
	sql_id = 0;

	remove(DB_FILE);
	rc = sqlite3_open(DB_FILE, &db);
	if(rc) {
		fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
		return(0);
	} else {
		fprintf(stderr, "Opened database successfully.\n");
	}

	/* Create SQL statement */
	sql = "CREATE TABLE DETECTIONS(" \
	"ID INT PRIMARY KEY     NOT NULL," \
	"IDSTATION      TEXT    NOT NULL," \
	"FREQUENCY      REAL    NOT NULL," \
	"BANDWIDTH      REAL    NOT NULL," \
	"DB             REAL);";

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);

	if( rc != SQLITE_OK ){
		fprintf(stderr, "SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
		fprintf(stdout, "Table created successfully.\n");
	}

	mosq = mosquitto_new(NULL, true, NULL);
	if(mosq == NULL){
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	mosquitto_connect_callback_set(mosq, on_connect_receiver);
	mosquitto_subscribe_callback_set(mosq, on_subscribe);
	mosquitto_message_callback_set(mosq, on_message);

	mosquitto_username_pw_set(mosq, parsed_data.username, parsed_data.password);
	rc = mosquitto_connect(mosq, parsed_data.hostname, parsed_data.port, parsed_data.keep_alive);
	if(rc != MOSQ_ERR_SUCCESS){
		mosquitto_destroy(mosq);
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
		return 1;
	}

	/* Run the network loop in a blocking call. The only thing we do in this
	 * example is to print incoming messages, so a blocking call here is fine.
	 *
	 * This call will continue forever, carrying automatic reconnections if
	 * necessary, until the user calls mosquitto_disconnect().
	 */
	mosquitto_loop_forever(mosq, -1, 1);

	sqlite3_close(db);
	mosquitto_lib_cleanup();
	return 0;
}

int mqtt_setup() {
	mosquitto_lib_init();
	int rc;
	
	stations_list = NULL;
	number_of_stations = 0;
	
	parse_configuration_file(&parsed_data);
	
	mosq = mosquitto_new(NULL, true, NULL);
	if(mosq == NULL){
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}
	
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_publish_callback_set(mosq, on_publish);
	
	mosquitto_username_pw_set(mosq, parsed_data.username, parsed_data.password);
	rc = mosquitto_connect(mosq, parsed_data.hostname, parsed_data.port, parsed_data.keep_alive);
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
	
	return 0;
}

void mqtt_cleanup() {
	mosquitto_lib_cleanup();
}
