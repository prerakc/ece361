#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

#include "packet.h"

void *get_in_addr(struct sockaddr *sa);
void *receive(void *sockfd_p);
int login(char *args, int *sockfd_p, pthread_t *receive_thread_p);
int logout(int *sockfd_p, pthread_t *receive_thread_p);
int join_session(char *args, int *sockfd_p);
int leave_session(char *args, int sockfd);
int create_session(char *args, int *sockfd_p);
int list(int sockfd);
int message(int sockfd);
int whisper(char *args, int sockfd);


char buf[BUF_SIZE];
int num_sessions = 0;

int main (int argc, char **argv) {
	int sockfd = -1;
	pthread_t receive_thread;
	char *cmd;
	int err;

	while (1) {
		do {
			memset(buf, 0, BUF_SIZE);
			fgets(buf, BUF_SIZE - 1, stdin);
			buf[strcspn(buf, "\n")] = '\0';
		} while (buf[0] == '\0');
		
		char *tmp = calloc(1, BUF_SIZE);
		memcpy(tmp, buf, BUF_SIZE);
		cmd = strsep(&tmp, " ");
		
		if (strcmp(cmd, "/login") == 0) {
			err = login(tmp, &sockfd, &receive_thread);
		} else if (strcmp(cmd, "/logout") == 0) {
			err = logout(&sockfd, &receive_thread);
		} else if (strcmp(cmd, "/joinsession") == 0) {
			err = join_session(tmp, &sockfd);
		} else if (strcmp(cmd, "/leavesession") == 0) {
			err = leave_session(tmp, sockfd);
		} else if (strcmp(cmd, "/createsession") == 0) {
			err = create_session(tmp, &sockfd);
		} else if (strcmp(cmd, "/list") == 0) {
			err = list(sockfd);
		} else if (strcmp(cmd, "/quit") == 0) {
			err = logout(&sockfd, &receive_thread);
			break;
		} else if (strcmp(cmd, "/whisper") == 0) {
			err = whisper(tmp, sockfd);
		} else {
			err = message(sockfd);
		}
	}
	
	printf("Successfully exited client program.\n");
	
	return 0;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *receive(void *sockfd_p) {
	int numbytes = 0;
	Packet packet;

	char *user;
	char *sess;
	
	while (1) {
		do {
			if ((numbytes = recv(*((int *)sockfd_p), buf, BUF_SIZE - 1, 0)) == -1) {
				fprintf(stderr, "client receive: recv\n");
				return NULL;
			}
		} while (numbytes == 0);
		
		buf[numbytes] = '\0';
		
		string_to_packet(buf, &packet);

		switch(packet.type) {
			case JN_ACK:
				printf("Successfully joined session %s.\n", packet.data);
				num_sessions = num_sessions + 1;
				break;
				
			case JN_NAK:
				printf("Failed to join session because %s\n", packet.data);
				break;

			case LS_ACK:
				printf("Successfully left session %s.\n", packet.data);
				num_sessions = num_sessions - 1;
				break;
				
			case LS_NAK:
				printf("Failed to leave session because %s\n", packet.data);
				break;
				
			case NS_ACK:
				printf("Successfully created and joined session %s.\n", packet.data);
				num_sessions = num_sessions + 1;
				break;
				
			case MESSAGE:
				user = strtok(packet.source, " ");
				sess = strtok(NULL, " ");
				printf("(%s)\t%s:\t%s\n", sess, user, packet.data);
				break;
				
			case QU_ACK:
				printf("User id\t\tSession ids\n%s", packet.data);
				break;

			case W_NAK:
				printf("Failed to send private message because %s\n", packet.data);
				break;
				
			default:
				printf("Recieved unexpected packet of type %d containing: %s\n", 
					packet.type, packet.data);
		}
		
		fflush(stdout);
	}
	
	return NULL;
}

int login(char *args, int *sockfd_p, pthread_t *receive_thread_p) {
	if (*sockfd_p != -1) {
		fprintf(stdout, "Already logged into a server.\n");
		return -1;
	}
	
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
		if ((*sockfd_p = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			fprintf(stderr ,"client: socket\n");
			continue;
		}
		
		if (connect(*sockfd_p, p->ai_addr, p->ai_addrlen) == -1) {
			close(*sockfd_p);
			fprintf(stderr, "client: connect\n");
			continue;
		}
		
		break; 
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect from addrinfo\n");
		close(*sockfd_p);
		*sockfd_p = -1;
		return -1;
	}
	
	printf("client: connecting to %s\n", inet_ntop(p->ai_family,
		get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s));
	
	freeaddrinfo(servinfo);
	
	int numbytes;
    Packet packet;
	
    packet.type = LOGIN;
    memcpy(packet.source, client_id, MAX_NAME);
    memcpy(packet.data, password, MAX_DATA);
	packet.size = strlen(packet.data);
	
	packet_to_string(&packet, buf);
	
	if ((numbytes = send(*sockfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		close(*sockfd_p);
		*sockfd_p = -1;
		return -1;
	}
	
	if ((numbytes = recv(*sockfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: recv\n");
		close(*sockfd_p);
		*sockfd_p = -1;
		return -1;
	}
	
	buf[numbytes] = '\0';
	
	string_to_packet(buf, &packet);
	
	if (packet.type != LO_ACK) {
		printf("Login failed. Recieved packet of type %d containing: %s\n", packet.type, packet.data);
		close(*sockfd_p);
		*sockfd_p = -1;
		return -1;
	} else if (pthread_create(receive_thread_p, NULL, receive, sockfd_p) != 0) {
		printf("Login failed because receive thread could not be made.\n");
		close(*sockfd_p);
		*sockfd_p = -1;
		return -1;
    } else {
		printf("Successfully logged in.\n");
		return 0;
	}
}

int logout(int *sockfd_p, pthread_t *receive_thread_p) {
	if (*sockfd_p == -1) {
		printf("Not logged into any server.\n");
		return -1;
	}
	
	int numbytes;
	Packet packet;
	
	packet.type = EXIT;
	packet.size = 0;
	
	packet_to_string(&packet, buf);
	
	if ((numbytes = send(*sockfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1;
	}
	
	int ret = 0;
	if (pthread_cancel(*receive_thread_p) != 0)	{
		printf("Logged out from server but receive thread still exists.\n");
		ret = -1;
	} else {
		printf("Successfully logged out.\n");
	}
	
	num_sessions = 0;
	close(*sockfd_p);
	*sockfd_p = -1;
	
	return ret;
}

int join_session(char *args, int *sockfd_p) {
	if (*sockfd_p == -1) {
		printf("Not logged into any server.\n");
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
	
    packet_to_string(&packet, buf);
	
	if ((numbytes = send(*sockfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1;
	}
	
	return 0;
}

int leave_session(char *args, int sockfd) {
	if (sockfd == -1) {
		printf("Not logged into any server.\n");
		return -1;
	} else if (num_sessions == 0) {
		printf("Not joined any session.\n");
		return -1;
	}
	
	char *session_id = args;
	
	if (session_id == NULL) {
		fprintf(stderr, "usage: /leavesession <session_id>\n");
		return -1;
	}

	int numbytes;
	Packet packet;
	
	packet.type = LEAVE_SESS;
	memcpy(packet.data, session_id, MAX_DATA);
	packet.size = strlen(packet.data);
	
	packet_to_string(&packet, buf);
	
	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}
		
	return 0;
}

int create_session(char *args, int *sockfd_p) {
	if (*sockfd_p == -1) {
		printf("Not logged into any server.\n");
		return -1;
	}
	
	char *session_id = args;
	
	if (session_id == NULL) {
		fprintf(stderr, "usage: /createsession <session_id>\n");
		return -1;
	}
	
	if (strcmp(session_id, "private") == 0) {
		fprintf(stderr, "session id cannot be 'private'\n");
		return -1;
	}

	int numbytes;
	Packet packet;
  
	packet.type = NEW_SESS;
	memcpy(packet.data, session_id, MAX_DATA);
    packet.size = strlen(packet.data);
	
	packet_to_string(&packet, buf);
	
	if ((numbytes = send(*sockfd_p, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}

	return 0;
}

int list(int sockfd) {
	if (sockfd == -1) {
		printf("Not logged into any server.\n");
		return -1;
	}
	
	int numbytes;
	Packet packet;
	
	packet.type = QUERY;
	packet.size = 0;
	
	packet_to_string(&packet, buf);
	
	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}
	
	return 0;
}

int message(int sockfd) {
	if (sockfd == -1) {
		printf("Not logged into any server.\n");
		return -1;
	} else if (num_sessions == 0) {
		printf("Not joined any session.\n");
		return -1;
	}

	int numbytes;
	Packet packet;
		
	packet.type = MESSAGE;
	memcpy(packet.data, buf, MAX_DATA);
	packet.size = strlen(packet.data);
		
	packet_to_string (&packet, buf);

	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}
	
	return 0;	
}

int whisper(char *args, int sockfd) {
	if (sockfd == -1) {
		printf("Not logged into any server.\n");
		return -1;
	}

	if (args == NULL) {
		fprintf(stderr, "usage: /whisper <user_id> <message>\n");
		return -1;
	}

	char *user_id = strsep(&args, " ");

	if (args == NULL) {
		fprintf(stderr, "usage: /whisper <user_id> <message>\n");
		return -1;
	}

	char *msg = args;
	
	int numbytes;
	Packet packet;
		
	packet.type = WHISPER;
	memcpy(packet.source, user_id, MAX_DATA);
	memcpy(packet.data, msg, MAX_DATA);
	packet.size = strlen(packet.data);
		
	packet_to_string (&packet, buf);

	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
		return -1; 
	}
	
	return 0;
}