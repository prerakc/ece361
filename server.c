#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "packet.h"

#define MAX_NAME 32
#define MAX_PSWD 32
#define BACKLOG 10
#define USER_NUM 3
#define MAX_USERS_SESSION 128
#define MAX_SESSIONS_USER 128
#define MAX_USERS 128
#define MAX_SESSIONS 128
#define MAX_SESSION_ID 32

typedef struct session {
	int sid;
	int num_users;
	
	char session_id[MAX_SESSION_ID];
	
	struct user *session_users[MAX_USERS_SESSION];
	struct user *creator;
} Session;

typedef struct user {
		char name[MAX_NAME];
		char pswd[MAX_PSWD];
		
		struct session *curr[MAX_SESSIONS_USER];
		
		int sockfd;
		int active;
} User;

Session *sessions[MAX_SESSIONS];

User users[USER_NUM];

char buf[BUF_SIZE] = {0};

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void login (int sockfd, Packet packet, Packet ret) {
	int numbytes;
							
	int found = -1;
	for (int i = 0; i < USER_NUM; i++) {
		if (strcmp(users[i].name, packet.source) == 0 && strcmp(users[i].pswd, packet.data) == 0) {
			found = i;
			break;
		}
	}
	
	if (found == -1) {
		ret.type = LO_NAK;
		ret.size = strlen("Invalid login credentials.\n");
		strcpy(ret.data, "Invalid login credentials.\n");
		
		packet_to_string(&ret, buf);
		
		if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
			fprintf(stderr, "client: send\n");
		}
	} else if (users[found].active == 1) {
		ret.type = LO_NAK;
		ret.size = strlen("User already signed in.\n");
		strcpy(ret.data, "User already signed in.\n");
		
		packet_to_string(&ret, buf);
		
		if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
			fprintf(stderr, "client: send\n");
		}
	} else {
		users[found].active = 1;
		users[found].sockfd = sockfd;
		
		ret.type = LO_ACK;
		ret.size = strlen("User signed in.\n");
		strcpy(ret.data, "User signed in.\n");
		
		packet_to_string(&ret, buf);
		
		if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
			fprintf(stderr, "client: send\n");
		}
	}
}

void leavesession (int sockfd, Packet packet, Packet ret) {
	// find user
	int found = -1;
	for (int f = 0; f < USER_NUM; f++) {
		if (users[f].sockfd == sockfd) {
			found = f;
			break;
		}
	}

	int numbytes;
	
	// check if session exists
	int session_exists = -1;
	for (int s = 0; s < MAX_SESSIONS; s++) {
		if (sessions[s] != NULL &&
				strcmp(sessions[s]->session_id, packet.data) == 0) {
			session_exists = s;
			break;
		}
	}

	// return NAK if session doesnt exist
	if (session_exists == -1) {
		ret.type = LS_NAK;
		strcpy(ret.data, "No such session exists.\n");
		ret.size = strlen(ret.data);

		packet_to_string(&ret, buf);

		if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
			fprintf(stderr, "server: send\n");
		}

		return;
	}

	int i = session_exists;
	int j = 0;
	int k = 0;

	// check if user in session
	for (j = 0; j < MAX_SESSIONS_USER; j++) {
		if (users[found].curr[j] != NULL) {
			if (strcmp(users[found].curr[j]->session_id, packet.data) == 0) {
				goto leavesession_foundsession;
			}
		}
	}

	// return NAK as user not in specified session
	ret.type = LS_NAK;
	strcpy(ret.data, "User not in specified session.\n");
	ret.size = strlen(ret.data);

	packet_to_string(&ret, buf);

	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "server: send\n");
	}

	return;

leavesession_foundsession:
	// leave session
	
	//printf("leavesession found session\n");

	sessions[i]->num_users = sessions[i]->num_users - 1;

	for (k = 0; k < MAX_USERS_SESSION; k++) {
		if (sessions[i]->session_users[k] == &(users[found])) {
			sessions[i]->session_users[k] = NULL;
			users[found].curr[j] = NULL;
			break;
		}
	}

	if (sessions[i]->num_users == 0) {
		free(sessions[i]);
				
		//printf("leavesession deleting session\n");
	}

	// return ACK to confirm user has left session
	ret.type = LS_ACK;
	strcpy(ret.data, packet.data);
	ret.size = strlen(ret.data);

	packet_to_string(&ret, buf);

	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "server: send\n");
	}
}

void logout (int sockfd, Packet packet) {								
	int found = -1;
	for (int f = 0; f < USER_NUM; f++) {
		if (users[f].sockfd == sockfd) {
			found = f;
			break;
		}
	}
	
	//printf("logout found user\n");
	
	int i = 0;
	int j = 0;
	int k = 0;

	// leave sessions one at a time
	for (j = 0; j < MAX_SESSIONS_USER; j++) {
		if (users[found].curr[j] != NULL) {
			for (i = 0; i < MAX_SESSIONS; i++) {
				if (sessions[i] != NULL) {
					if (strcmp(sessions[i]->session_id, users[found].curr[j]->session_id) == 0) {
						sessions[i]->num_users = sessions[i]->num_users - 1;

						for (k = 0; k < MAX_USERS_SESSION; k++) {
							if (sessions[i]->session_users[k] == &(users[found])) {
								sessions[i]->session_users[k] = NULL;
								users[found].curr[j] = NULL;
								break;
							}
						}

						if (sessions[i]->num_users == 0) {
							free(sessions[i]);
						}

						break;
					}
				}
			}
		}
	}

	users[found].active = 0;
	users[found].sockfd = -1;
}

void joinsession (int sockfd, Packet packet, Packet ret) {
	int found = -1;
	for (int i = 0; i < USER_NUM; i++) {
		if (users[i].sockfd == sockfd) {
			found = i;
			break;
		}
	}
	
	int nosuchsession = 1;
	int alreadyinsession = 0;
	int k = 0;
	
	for (int i = 0; i < MAX_SESSIONS; i++) {
		if (sessions[i] == NULL) {
			continue;
		}
		
		if (strcmp(sessions[i]->session_id, packet.data) == 0) {
			nosuchsession = 0;

			for (int j = 0; j < MAX_USERS_SESSION; j++) {
				if (sessions[i]->session_users[j] != NULL &&
						sessions[i]->session_users[j] == &users[found]) {
					alreadyinsession = 1;
					break;
				}
			}

			if (alreadyinsession == 0) {
				sessions[i]->num_users = sessions[i]->num_users + 1;

				for (int j = 0; j < MAX_USERS_SESSION; j++) {
					if (sessions[i]->session_users[j] == NULL) {
						sessions[i]->session_users[j] = &users[found];
						
						for (k = 0; k < MAX_SESSIONS_USER; k++) {
							if (users[found].curr[k] == NULL) {
								users[found].curr[k] = sessions[i];
								break;
							}
						}

						break;
					}
				}
			}

			break;
		}
	}
	
	if (nosuchsession == 1) {
		ret.type = JN_NAK;
		strcpy(ret.data, "No such session exists.\n");
		ret.size = strlen(ret.data);
	} else if (alreadyinsession == 1) {
		ret.type = JN_NAK;
		strcpy(ret.data, "Already in specified session.\n");
		ret.size = strlen(ret.data);
	} else {
		ret.type = JN_ACK;
		strcpy(ret.data, users[found].curr[k]->session_id);
		ret.size = strlen(ret.data);
	}
		
	packet_to_string(&ret, buf);
	
	int numbytes;
	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "server: send\n");
	}
}

void createsession (int sockfd, Packet packet, Packet ret) {
	int found = -1;
	for (int i = 0; i < USER_NUM; i++) {
		if (users[i].sockfd == sockfd) {
			found = i;
			break;
		}
	}
	
	int alreadyexists = 0;
	
	for (int i = 0; i < MAX_SESSIONS; i++) {
		if (sessions[i] != NULL) {
			if (strcmp(sessions[i]->session_id, packet.data) == 0) {
				ret.type = JN_NAK;
				strcpy(ret.data, "Session already exists.\n");
				ret.size = strlen(ret.data);
				
				alreadyexists = 1;
				
				break;
			}
		}
	}
	
	if (alreadyexists == 0) {
		for (int i = 0; i < MAX_SESSIONS; i++) {
			if (sessions[i] == NULL) {
				sessions[i] = calloc(1, sizeof(Session));
				sessions[i]->sid = i;
				sessions[i]->num_users = 1;
				strcpy(sessions[i]->session_id, packet.data);
				sessions[i]->session_users[0] = &users[found];
				sessions[i]->creator = &users[found];

				for (int j = 0; j < MAX_SESSIONS_USER; j++) {
					if (users[found].curr[j] == NULL) {
						users[found].curr[j] = sessions[i];
						break;
					}
				}
				
				ret.type = NS_ACK;
				ret.size = packet.size;
				strcpy(ret.data, packet.data);
		
				break;
			}
		}
	}
	
	packet_to_string(&ret, buf);
	
	int numbytes;
	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
	}
}

void list (int sockfd, Packet packet, Packet ret) {
	char tmp[BUF_SIZE];
	
	memset(ret.data, 0, BUF_SIZE);
	
	for (int i = 0; i < USER_NUM; i++) {
		if (users[i].active == 0) {
			continue;
		}
		
		memset(tmp, 0, BUF_SIZE);
		int offset = snprintf(tmp, BUF_SIZE, "%s\t\t", users[i].name);

		for (int j = 0; j < MAX_SESSIONS_USER; j++) {
			if (users[i].curr[j] == NULL) {
				continue;
			}

			offset = offset + snprintf(tmp+offset, BUF_SIZE, "%s\t", users[i].curr[j]->session_id);
		}
		
		strcat(tmp, "\n");
		strcat(ret.data, tmp);
	}
	
	ret.type = QU_ACK;
	ret.size = strlen(ret.data);
		
	packet_to_string(&ret, buf);
	
	int numbytes;
	if ((numbytes = send(sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
		fprintf(stderr, "client: send\n");
	}
}

void message (int sockfd, Packet packet, Packet ret) {	
	int found = -1;
	for (int i = 0; i < USER_NUM; i++) {
		if (users[i].sockfd == sockfd) {
			found = i;
			break;
		}
	}
	
	ret.type = MESSAGE;
	strcpy(ret.data, packet.data);
	ret.size = strlen(ret.data);
	
	int numbytes;
	
	for (int j = 0; j < MAX_SESSIONS_USER; j++) {
		if (users[found].curr[j] == NULL) {
			continue;
		}

		memset(ret.source, 0, MAX_NAME);
		strcpy(ret.source, users[found].name);
		strcat(ret.source, " ");
		strcat(ret.source, users[found].curr[j]->session_id);

		packet_to_string(&ret, buf);
		
		for (int i = 0; i < MAX_USERS_SESSION; i++) {
			if (users[found].curr[j]->session_users[i] == NULL) {
				continue;
			}
			
			if ((numbytes = send(users[found].curr[j]->session_users[i]->sockfd, buf, BUF_SIZE - 1, 0)) == -1) {
				fprintf(stderr, "client: send\n");
			}
		}
	}
}

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port number>\n>", argv[0]);
		return 1;
	}

	char *endptr;
	long int port = strtol(argv[1], &endptr, 10);
		
	if (endptr == argv[1]) {
		printf("ERROR: Invalid number: %s\n", argv[1]);
		return 1;
	} else if (*endptr) {
		fprintf(stderr, "ERROR: Trailing characters after number: %s\n", argv[1]); 
		return 1;
	} else if (errno == ERANGE || port > 65535 || port < 1024) {
		fprintf(stderr, "ERROR: Number out of range: %s\n", argv[1]); 
		return 1;
	}
	
	char port_str[6];
	sprintf(port_str, "%d", (int)port);
	
	for (int i = 0; i < USER_NUM; i++) {
		users[i].active = 0;
		users[i].sockfd = -1;
		snprintf(users[i].name, MAX_NAME, "u%d", i+1);
		snprintf(users[i].pswd, MAX_PSWD, "p%d", i+1);
	}

	
	fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    int fdmax;        // maximum file descriptor number

    int listener;     // listening socket descriptor
    int newfd;        // newly accept()ed socket descriptor
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen;

    int nbytes;

    char remoteIP[INET6_ADDRSTRLEN];

    int yes=1;        // for setsockopt() SO_REUSEADDR, below
    int i, j, rv;

    struct addrinfo hints, *ai, *p;

    FD_ZERO(&master);    // clear the master and temp sets
    FD_ZERO(&read_fds);

    // get us a socket and bind it
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if ((rv = getaddrinfo(NULL, port_str, &hints, &ai)) != 0) {
        fprintf(stderr, "selectserver: %s\n", gai_strerror(rv));
        exit(1);
    }
    
    for(p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) { 
            continue;
        }
        
        // lose the pesky "address already in use" error message
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }

        break;
    }

    // if we got here, it means we didn't get bound
    if (p == NULL) {
        fprintf(stderr, "selectserver: failed to bind\n");
        exit(2);
    }

    freeaddrinfo(ai); // all done with this

    // listen
    if (listen(listener, BACKLOG) == -1) {
        perror("listen");
        exit(3);
    }

    // add the listener to the master set
    FD_SET(listener, &master);

    // keep track of the biggest file descriptor
    fdmax = listener; // so far, it's this one

    // main loop
    for(;;) {
        read_fds = master; // copy it
        if (select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        // run through the existing connections looking for data to read
        for(i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_fds)) { // we got one!!
                if (i == listener) {
                    // handle new connections
                    addrlen = sizeof remoteaddr;
                    newfd = accept(listener,
                        (struct sockaddr *)&remoteaddr,
                        &addrlen);

                    if (newfd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(newfd, &master); // add to master set
                        if (newfd > fdmax) {    // keep track of the max
                            fdmax = newfd;
                        }
                        printf("selectserver: new connection from %s on "
                            "socket %d\n",
                            inet_ntop(remoteaddr.ss_family,
                                get_in_addr((struct sockaddr*)&remoteaddr),
                                remoteIP, INET6_ADDRSTRLEN),
                            newfd);
                    }
                } else {
                    // handle data from a client
                    if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
                        // got error or connection closed by client
                        if (nbytes == 0) {
                            // connection closed
                            printf("selectserver: socket %d hung up\n", i);
                        } else {
                            perror("recv");
                        }
                        close(i); // bye!
                        FD_CLR(i, &master); // remove from master set
                    } else {
                        // we got some data from a client
						buf[nbytes] = '\0';
						printf("recieved %s\n", buf);
						
						Packet packet, ret;
						
						string_to_packet(buf, &packet);
						
						//printf("%s\t%s\n", users[0].name, packet.source);
						//printf("%s\t%s\n", users[0].pswd, packet.data);
						
						if (packet.type == LOGIN) {
							login(i, packet, ret);
						} else if (packet.type == EXIT) {
							logout(i, packet);
						} else if (packet.type == JOIN) {
							joinsession(i, packet, ret);
						} else if (packet.type == LEAVE_SESS) {
							leavesession (i, packet, ret);
						} else if (packet.type == NEW_SESS) {
							createsession (i, packet, ret);
						} else if (packet.type == QUERY) {
							list(i, packet, ret);
						} else if (packet.type == MESSAGE) {
							message(i, packet, ret);
						}
                    }
                } // END handle data from client
            } // END got new incoming connection
        } // END looping through file descriptors
    } // END for(;;)--and you thought it would never end!
    
    return 0;
}