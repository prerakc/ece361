#ifndef PACKET_H
#define PACKET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAME 32
#define MAX_DATA 512

#define BUF_SIZE 512

#define LOGIN 1
#define LO_ACK 2
#define LO_NAK 3
#define EXIT 4
#define JOIN 5
#define JN_ACK 6
#define JN_NAK 7
#define LEAVE_SESS 8
#define LS_ACK 9
#define LS_NAK 10
#define NEW_SESS 11
#define NS_ACK 12
#define MESSAGE 13
#define QUERY 14
#define QU_ACK 15
#define WHISPER 16
#define W_ACK 17
#define W_NAK 18


typedef struct message {
    unsigned int type;
    unsigned int size;
    unsigned char source[MAX_NAME];
    unsigned char data[MAX_DATA];
} Packet;


// convert struct contents into text packet
void packet_to_string(Packet *packet, char *dest) {
    memset(dest, 0, sizeof(char) * BUF_SIZE);
	snprintf(dest, BUF_SIZE, "%d:%d:%s:%s", packet->type, packet->size, packet->source, packet->data);
}

// populate message struct with contents of text packet
void string_to_packet(char *str, Packet *dest_packet) {
	memset(dest_packet -> data, 0, MAX_DATA);
	
	if (strlen(str) == 0) return;
	
	char *tmp = calloc(1, BUF_SIZE);
	memcpy(tmp, str, BUF_SIZE);
	
	dest_packet->type = atoi(strsep(&tmp, ":"));
	dest_packet->size = atoi(strsep(&tmp, ":"));
	
	if (dest_packet->size == 0) {
		return;
	}
	
	strcpy(dest_packet->source, strsep(&tmp, ":"));
	strcpy(dest_packet->data, tmp);
}

#endif