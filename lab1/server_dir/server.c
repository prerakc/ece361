#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAXBUFLEN 100

// get sockaddr, IPv4 or IPv6:
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
	}
	
	char port_str[6];
	sprintf(port_str, "%d", (int)port);

	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
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

	freeaddrinfo(servinfo);
	
	printf("server: waiting to recvfrom...\n");
	
	addr_len = sizeof their_addr;
	if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
		(struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("server: recvfrom");
		exit(1);
	}
	
	printf("server: got packet from %s\n",
		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s));
	printf("server: packet is %d bytes long\n", numbytes);
	buf[numbytes] = '\0';
	printf("server: packet contains \"%s\"\n", buf);

	
	int numretbytes;
	char *retstr;
	
	if (strcmp(buf, "ftp") == 0) {
		retstr = "yes";
	} else {
		retstr = "no";
	}
	
	if ((numretbytes = sendto(sockfd, retstr, strlen(retstr), 0,
			(struct sockaddr *)&their_addr, addr_len)) == -1) {
		perror("server: sendto");
		exit(1);
	}
	
	printf("server: sending \"%s\" over %d bytes to %s\n", retstr, numretbytes, inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s));
	
	close(sockfd);
 	
	return 0;
}
