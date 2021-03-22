#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>

#include "packet.h"

#define INVALID_SOCKET - 1

void *get_in_addr(struct sockaddr *sa);
void *receive(void *socketfd_void_p);
int login(char *args, int *socketfd_p, pthread_t *receive_thread_p);
int logout(int *socketfd_p, pthread_t *receive_thread_p);
int join_session(char *args, int *socketfd_p);
int leave_session(int socketfd);
int create_session(char *args, int *socketfd_p);
int list(int socketfd);
int message(int socketfd);

char buf[BUF_SIZE] = {0};
int in_session = 0;

int main (int argc, char **argv) {
	int socketfd = INVALID_SOCKET;
	pthread_t receive_thread;
	char *cmd;
	
	while (1) {
		fgets(buf, BUF_SIZE - 1, stdin);
		buf[strcspn(buf, "\n")] = '\0';
		
		cmd = buf;

		while (*cmd == ' ') {
			cmd = cmd + 1;
		}
		
		if (*cmd == '\0') {
			continue;
		}
		
		char *tmp = calloc(1, BUF_SIZE);
		memcpy(tmp, buf, BUF_SIZE);
		cmd = strsep(&tmp, " ");
		
		if (strcmp(cmd, "/login") == 0) {
			login(tmp, &socketfd, &receive_thread);
		} else if (strcmp(cmd, "/logout") == 0) {
			logout(&socketfd, &receive_thread);
		} else if (strcmp(cmd, "/joinsession") == 0) {
			join_session(tmp, &socketfd);
		} else if (strcmp(cmd, "/leavesession") == 0) {
			leave_session(socketfd);
		} else if (strcmp(cmd, "/createsession") == 0) {
			create_session(tmp, &socketfd);
		} else if (strcmp(cmd, "/list") == 0) {
			list(socketfd);
		} else if (strcmp(cmd, "/quit") == 0) {
			logout(&socketfd, &receive_thread);
			break;
		} else {
			message(socketfd);
		}
	}
	
	printf("Successfully exited client program.\n");
	
	return 0;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
	}
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *receive(void *socketfd_void_p) {
	int *socketfd_p = (int *)socketfd_void_p;
	int numbytes = 0;
	Packet packet;
	
	while (1) {
		do {
			if ((numbytes = recv(*socketfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
				fprintf(stderr, "client receive: recv\n");
				return NULL;
			}
		} while (numbytes == 0);
		
		buf[numbytes] = '\0';
		
		stringToPacket(buf, &packet);

		if (packet.type == JN_ACK) {
			printf("Successfully joined session %s.\n", packet.data);
			in_session = 1;
		} else if (packet.type == JN_NAK) {
			printf("Failed to join session because %s\n", packet.data);
			in_session = 0;
		} else if (packet.type == NS_ACK) {
			printf("Successfully created and joined session %s.\n", packet.data);
			in_session = 1;
		} else if (packet.type == QU_ACK) {
			printf("User id\t\tSession ids\n%s", packet.data);
		} else if (packet.type == MESSAGE){   
			printf("%s: %s\n", packet.source, packet.data);
		} else {
			printf("Recieved unexpected packet of type %d containing: %s\n",
				packet.type, packet.data);
		}
		
		fflush(stdout);
	}
	
	return NULL;
}

int login(char *args, int *socketfd_p, pthread_t *receive_thread_p) {
	char *client_id = strsep(&args, " ");
	char *password = strsep(&args, " ");
	char *server_ip = strsep(&args, " ");
	char *server_port = args;

	if (client_id == NULL || password == NULL || server_ip == NULL || server_port == NULL) {
		fprintf(stderr, "usage: /login <client_id> <password> <server_ip> <server_port>\n");
		return -1;
	}
	
	char *endptr;
	long int port = strtol(server_port, &endptr, 10);
		
	if (endptr == server_port) {
		fprintf(stderr, "error: %s is an invalid number\n", server_port);
		return -1;
	} else if (*endptr) {
		fprintf(stderr, "error: trailing characters after number %s\n", server_port); 
		return -1;
	} else if (errno == ERANGE || port > 65535 || port < 1024) {
		fprintf(stderr, "error: port number %s is out of range\n", server_port); 
		return -1;
	}
	
	if (*socketfd_p != INVALID_SOCKET) {
		fprintf(stdout, "Already logged into a server.\n");
		return -1;
	}
	
	int rv;
	struct addrinfo hints, *servinfo, *p;
	char s[INET6_ADDRSTRLEN];
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(server_ip, server_port, &hints, &servinfo)) != 0) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return -1;
	}
	
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((*socketfd_p = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			fprintf(stderr ,"client: socket\n");
			continue;
		}
		
		if (connect(*socketfd_p, p->ai_addr, p->ai_addrlen) == -1) {
			close(*socketfd_p);
			fprintf(stderr, "client: connect\n");
			continue;
		}
		
		break; 
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect from addrinfo\n");
		close(*socketfd_p);
		*socketfd_p = INVALID_SOCKET;
		return -1;
	}
	
	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
	printf("client: connecting to %s\n", s);
	freeaddrinfo(servinfo);
	
	int numbytes;
    Packet packet;
	
    packet.type = LOGIN;
    memcpy(packet.source, client_id, MAX_NAME);
    memcpy(packet.data, password, MAX_DATA);
	packet.size = strlen(packet.data);
	
	packetToString(&packet, buf);
	
	if ((numbytes = send(*socketfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		close(*socketfd_p);
		*socketfd_p = INVALID_SOCKET;
		return -1;
	}
	
	if ((numbytes = recv(*socketfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: recv\n");
		close(*socketfd_p);
		*socketfd_p = INVALID_SOCKET;
		return -1;
	}
	
	buf[numbytes] = '\0';
	
	stringToPacket(buf, &packet);
	
    if (packet.type == LO_ACK &&
			pthread_create(receive_thread_p, NULL, receive, socketfd_p) == 0) {
		printf("Successfully logged in.\n");
		return 0;
    }
	
	printf("Login failed. Recieved packet of type %d containing: %s\n", packet.type, packet.data);
	close(*socketfd_p);
    *socketfd_p = INVALID_SOCKET;
	
	return -1;
}

int logout(int *socketfd_p, pthread_t *receive_thread_p) {
	if (*socketfd_p == INVALID_SOCKET) {
		printf("Not logged into any server.\n");
		return -1;
	}
	
	int numbytes;
	Packet packet;
	
	packet.type = EXIT;
	packet.size = 0;
	
	packetToString(&packet, buf);
	
	if ((numbytes = send(*socketfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1;
	}
	
	int ret = 0;
	if (pthread_cancel(*receive_thread_p) == 0)	{
		printf("Successfully logged out.\n");
	} else {
		printf("Logout failed. Recieve thread still exists. \n");
		ret = -1;
	}
	
	in_session = 0;
	close(*socketfd_p);
	*socketfd_p = INVALID_SOCKET;
	
	return ret;
	
	
}

int join_session(char *args, int *socketfd_p) {
	if (*socketfd_p == INVALID_SOCKET) {
		printf("Not logged into any server.\n");
		return -1;
	} else if (in_session == 1) {
		printf("Already joined a session.\n");
		return -1;
	}
	
	char *session_id = args;
	
	if (session_id == NULL) {
		fprintf(stderr, "usage: /joinsession <session_id>\n");
		return -1;
	}
	
	int numbytes;
    Packet packet;
	
    packet.type = JOIN;
    memcpy(packet.data, session_id, MAX_DATA);
    packet.size = strlen(packet.data);
	
    packetToString(&packet, buf);
	
	if ((numbytes = send(*socketfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1;
	}
	
	return 0;
	
}

int leave_session(int socketfd) {
	if (socketfd == INVALID_SOCKET) {
		printf("Not logged into any server.\n");
		return -1;
	} else if (in_session == 0) {
		printf("Not joined any session.\n");
		return -1;
	}
	
	int numbytes;
	Packet packet;
	
	packet.type = LEAVE_SESS;
	packet.size = 0;
	
	packetToString(&packet, buf);
	
	if ((numbytes = send(socketfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}
	
	in_session = 0;
	
	return 0;
}

int create_session(char *args, int *socketfd_p) {
	if (*socketfd_p == INVALID_SOCKET) {
		printf("Not logged into any server.\n");
		return -1;
	} else if (in_session == 1) {
		printf("Already joined a session.\n");
		return -1;
	}
	
	char *session_id = args;
	
	if (session_id == NULL) {
		fprintf(stderr, "usage: /createsession <session_id>\n");
		return -1;
	}
	
	int numbytes;
	Packet packet;
  
	packet.type = NEW_SESS;
	memcpy(packet.data, session_id, MAX_DATA);
    packet.size = strlen(packet.data);
	
	packetToString(&packet, buf);
	
	if ((numbytes = send(*socketfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}

	return 0;
}

int list(int socketfd) {
	if (socketfd == INVALID_SOCKET) {
		printf("Not logged into any server.\n");
		return -1;
	}
	
	int numbytes;
	Packet packet;
	
	packet.type = QUERY;
	packet.size = 0;
	
	packetToString(&packet, buf);
	
	if ((numbytes = send(socketfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}
	
	return 0;
}

int message(int socketfd) {
	if (socketfd == INVALID_SOCKET) {
		printf("Not logged into any server.\n");
		return -1;
	} else if (in_session == 0) {
		printf("Not joined any session.\n");
		return -1;
	}

	int numbytes;
	Packet packet;
		
	packet.type = MESSAGE;
	memcpy(packet.data, buf, MAX_DATA);
	packet.size = strlen(packet.data);
		
	packetToString (&packet, buf);

	if ((numbytes = send(socketfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}
	
	return 0;	
}