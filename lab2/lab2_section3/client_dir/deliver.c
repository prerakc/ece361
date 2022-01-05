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
#include <math.h>

#define MAXBUFLEN 2000

void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

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
	} else if (port == 25566) {
		fprintf(stderr, "ERROR: Port 25566 used for ACK socket\n"); 
		return 1;
	}
	
	char port_str[6];
	sprintf(port_str, "%d", (int)port);
	
	char msg[MAXBUFLEN];
	int msglen;
	char *cmd, *filename;

	while (1) {
		memset(msg, 0, MAXBUFLEN);
		
		printf("Input a message as follows: ftp <file name>.<file extension>\n");
		printf("NOTE: I have not implemented a robust way to ensure this format is followed and files with multiple extensions (i.e. file.ext.bkup) are not supported\n");
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

	if (strstr(filename, ".") == NULL) {
		printf("error: enter the file extension after the name\n");
		return 1;
	}

	struct stat statbuffer;
	int file_exist = stat(filename, &statbuffer);
	
	if (file_exist == -1) {
		printf("%s does not exist\n", filename);
		return 1;
	}
	
	// connect to server socket
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	
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

	//setup client socket on port 25566
	int sockfd2;
	struct addrinfo hints2, *servinfo2, *q;
	int rv2;
	int numbytes2;
	//struct sockaddr_storage their_addr;
	char buf2[MAXBUFLEN];
	//socklen_t addr_len;
	//char s[INET6_ADDRSTRLEN];
	
	memset(&hints2, 0, sizeof hints2);
	hints2.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
	hints2.ai_socktype = SOCK_DGRAM;
	hints2.ai_flags = AI_PASSIVE; // use my IP
	
	if ((rv2 = getaddrinfo(NULL, "25566", &hints2, &servinfo2)) != 0) {
		fprintf(stderr, "client: getaddrinfo: %s\n", gai_strerror(rv2));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(q = servinfo2; q != NULL; q = q->ai_next) {
		if ((sockfd2 = socket(q->ai_family, q->ai_socktype, q->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}
		
		if (bind(sockfd2, q->ai_addr, q->ai_addrlen) == -1) {
			close(sockfd2);
			perror("client: bind");
			continue;
		}
	
		break;
	}

	if (q == NULL) {
		fprintf(stderr, "client: failed to bind socket\n");
		return 2;
	}
	
	// send packets
	int fd = open(filename, O_RDONLY);

	char filedata[1000] = {0};
	int size = read(fd, filedata, 1000);

	int total_frag = (int) ceil ((double)statbuffer.st_size / 1000.0);
	int frag_no = 0;

	char packet [2000] = {0};

	int ack_flag = 1;
		
	while (size != 0) {
		// make and send packet
		if (ack_flag == 1) {	
			// make
			frag_no = frag_no + 1;
			memset(packet, 0, 2000);
			int offset = snprintf (packet, 2000, "%d:%d:%d:%s:", total_frag, frag_no, size, filename);
			memcpy(packet+offset, filedata, size);
			//printf("%s\n", filedata);
			printf("%s\n\n", packet);
			// send
			if ((numbytes = sendto(sockfd, packet, 2000,
							0, p->ai_addr, p->ai_addrlen)) == -1) {
				perror("client: sendto packet");
				exit(1);
			}
			ack_flag = 0;
			//filedata = filedata + size;
		} else {

			// recieve acknowledge: set ack_flag = 1
			if ((numbytes2 = recvfrom(sockfd2, buf2, MAXBUFLEN-1, 0, q->ai_addr, &(q->ai_addrlen))) == -1) {
				perror("client: recvfrom packet");
				exit(1);
			}
			//printf("%s\n", buf2);
			//buf2[numbytes2] = '\0';
			
			if (strcmp(buf2, "ACK") == 0) {
				printf("recv ack\n\n");
				ack_flag = 1;
			}
		}

		if (ack_flag == 1) {
			size = read(fd, filedata, 1000);
		}
	}
	
	if ((numbytes = sendto(sockfd, "NACK", strlen("NACK"),
				0, p->ai_addr, p->ai_addrlen)) == -1) {
		perror("client: sendto packet");
		exit(1);
	}
	
	close(fd);
	
	freeaddrinfo(servinfo);
	freeaddrinfo(servinfo2);
	
	close(sockfd);
	close(sockfd2);

	return 0;
}
