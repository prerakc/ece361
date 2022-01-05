#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#define MAXBUFLEN 100

struct timeval time_difference(struct timeval t1, struct timeval t2);

int main (int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "USAGE: %s <server address> <server port number>\n", argv[0]);
		exit(1);
	}

	char *endptr;
	long int port = strtol(argv[2], &endptr, 10);
		
	if (endptr == argv[2]) {
		printf("ERROR: Invalid number: %s\n", argv[2]);
		return 1;
	} else if (*endptr) {
		fprintf(stderr, "ERROR: Trailing characters after number: %s\n", argv[2]); 
		return 1;
	} else if (errno == ERANGE || port > 65535 || port < 1024) {
		fprintf(stderr, "ERROR: Number out of range: %s\n", argv[2]); 
		return 1;
	}
	
	char port_str[6];
	sprintf(port_str, "%d", (int)port);
	
	char msg[MAXBUFLEN];
	int msglen;
	char *cmd, *filename;

	while (1) {
		memset(msg, 0, MAXBUFLEN);
		
		printf("Input a message as follows: ftp <file name>\n");
		fgets(msg, MAXBUFLEN, stdin);
		
		msglen = strlen(msg);
		
		if (msglen > 0 && msg[msglen-1] == '\n') {
			msg[msglen-1] = '\0';
		}
		
		cmd = strtok(msg, " ");
		
		if (strcmp(cmd, "ftp") != 0) {
			printf("Not an ftp command\n");
		} else {
			break;
		}
	}

	filename = strtok(NULL, " ");

	struct stat buffer;
	int file_exist = stat(filename, &buffer);
	
	if (file_exist == -1) {
		printf("%s does not exist\n", filename);
		return 1;
	}
	
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	char buf[MAXBUFLEN];
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints.ai_socktype = SOCK_DGRAM;
 
	if ((rv = getaddrinfo(argv[1], port_str, &hints, &servinfo)) != 0) {
		fprintf(stderr, "client: getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
	
	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}
	 
		break;
	}
	
	if (p == NULL) {
		fprintf(stderr, "client: failed to create socket\n");
		return 2;
	}

	struct timeval start, end;
	
	gettimeofday(&start, NULL);

	if ((numbytes = sendto(sockfd, cmd, strlen(cmd), 0,
			p->ai_addr, p->ai_addrlen)) == -1) {
		perror("client: sendto");
		exit(1);
	}
	
	printf("client: sent the string \"%s\" over %d bytes to %s\n", cmd, numbytes, argv[1]);
	
	if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
			p->ai_addr, &(p->ai_addrlen))) == -1) {
		perror("client: recvfrom");
		exit(1);
	}

	gettimeofday(&end, NULL);

	struct timeval delta = time_difference(start, end);

	printf("client: got packet from %s\n", argv[1]);
	printf("client: packet is %d bytes long\n", numbytes);
	buf[numbytes] = '\0';
	printf("client: packet contains \"%s\"\n", buf);
	
	if (strcmp(buf, "yes") == 0) {
		printf("A file transfer can start.\n");
	}
	
	printf("RTT time is %d.%06d\n", (int)delta.tv_sec, (int)delta.tv_usec);

	freeaddrinfo(servinfo);
	
	close(sockfd);
	
	return 0;
}

struct timeval time_difference(struct timeval t1, struct timeval t2) {
	struct timeval result;

	result.tv_sec = t2.tv_sec - t1.tv_sec;      // compute difference in seonds
	result.tv_usec = t2.tv_usec - t1.tv_usec;   // compute difference in microseconds

	// difference in microseconds could be negative so keep adding 10^6 microseconds and removing 1 second as needed
	for (;;) {
		if (result.tv_usec >= 0) {
			break;
		} else {
			result.tv_usec = result.tv_usec + 1000000;
			result.tv_sec = result.tv_sec - 1;
		}
	}
	
	return result;
}
