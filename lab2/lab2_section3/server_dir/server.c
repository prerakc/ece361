#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/dir.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>

#define MAXBUFLEN 2000

void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main (int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "USAGE: %s <UDP listen port>\n", argv[0]);
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
	} else if (port == 25566) {
		fprintf(stderr, "ERROR: Port 25566 used for ACK socket"); 
		return 1;
	}
	
	char port_str[6];
	sprintf(port_str, "%d", (int)port);
	
	// setup server socket
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes = 1;
	struct sockaddr_storage their_addr;
	char buf[MAXBUFLEN];
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP
	
	if ((rv = getaddrinfo(NULL, port_str, &hints, &servinfo)) != 0) {
		fprintf(stderr, "server: getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
		
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}
	
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "server: failed to bind socket\n");
		return 2;
	}
	
	// wait for client program to start
	sleep(10);
	
	// connect to client socket on 25566
	int sockfd2;
	struct addrinfo hints2, *servinfo2, *q;
	int rv2;
	int numbytes2;
	
	memset(&hints2, 0, sizeof hints2);
	hints2.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints2.ai_socktype = SOCK_DGRAM;
 
	int ack_flag = 0;


	char *total_frag;
	char *frag_no;
	char *size ;
	char *filename;
	char *filedata;
	
	int fd = -1;
	
	while (1) {
		//printf("loop: %d\n", loop);
		
		if (ack_flag == 1) {
			//printf("%d\n", 1);
			if (fd == -1) {
				fd = creat(filename, S_IRUSR | S_IWUSR);
			}
			
			write(fd, filedata, (int)atoi(size));
			
			/*
			if (pFile == NULL) {
				pFile = fopen (filename, "wb");
			}
			
			fwrite (filedata , sizeof(char), (int)atoi(size), pFile);
			*/

			ack_flag = 0;
			
			printf("sending ack\n\n");
						
			if ((numbytes2 = sendto(sockfd2, "ACK", strlen("ACK"), 0,
						q->ai_addr, q->ai_addrlen)) == -1) {
				perror("server: sendto ack");
				exit(1);
			}
		} else {
			memset(buf, 0, MAXBUFLEN);
			addr_len = sizeof their_addr;
			if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
				(struct sockaddr *)&their_addr, &addr_len)) == -1) {
				perror("server: recvfrom");
				exit(1);
			}
			
			if (fd == -1) {
				if ((rv2 = getaddrinfo(inet_ntop(their_addr.ss_family,
								get_in_addr((struct sockaddr *)&their_addr),
								s, sizeof s),
							"25566", &hints2, &servinfo2)) != 0) {
					fprintf(stderr, "client: getaddrinfo: %s\n", gai_strerror(rv2));
					return 1;
				}
				
				// loop through all the results and make a socket
				for(q = servinfo2; q != NULL; q = q->ai_next) {
					if ((sockfd2 = socket(q->ai_family, q->ai_socktype,
							q->ai_protocol)) == -1) {
						perror("client: socket");
						continue;
					}
				 
					break;
				}
				
				if (q == NULL) {
					fprintf(stderr, "client: failed to create socket\n");
					return 2;
				}	
			}
			
			if (strcmp(buf, "NACK") == 0) {
				//printf("yes\n");
				break;
			}
			ack_flag = 1;
			//buf[numbytes] = '\0';
		
			printf("PACKET: %s\n", buf);

			char *tmp = calloc(1, MAXBUFLEN);
			memcpy(tmp, buf, MAXBUFLEN);
			
			filedata = strstr(tmp, ".");
			strsep(&filedata, ":");

			total_frag = strsep(&tmp, ":");
			total_frag++;
			total_frag--;

			frag_no = strsep(&tmp, ":");
			frag_no++;
			frag_no--;

			size = strsep(&tmp, ":");

			filename = strsep(&tmp, ":");

			while (strstr(filename, ".") == NULL) {
				char *addon = strsep(&tmp, ":");
				char *concat = calloc(1, MAXBUFLEN);
				snprintf(concat, MAXBUFLEN, "%s:%s", filename, addon);
				filename = concat;
			}
			printf("\n");
		}
	}
	
	close(fd);
	
	freeaddrinfo(servinfo);
	freeaddrinfo(servinfo2);
	
	close(sockfd);
	close(sockfd2);
	
	return 0;
}
