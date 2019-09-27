#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#define MAXLINE 1024

int main(int argc, char** argv){

	int startPort;
	int endPort;
	int sockfd;
	fd_set read_fds;
    char buffer[MAXLINE];
    char *hello = "Hello from server";
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    bzero(&servaddr, sizeof(servaddr));

    if(argc != 3){
        printf("Not correct amount of arguments\n");
        return EXIT_FAILURE;
    }

    startPort = atoi(argv[1]);
    endPort = atoi(argv[2]);

    if(startPort > endPort){
        printf("Startport is before endPort\n");
        return EXIT_FAILURE;
    }

    printf("start port: %d\n", startPort);

    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Filling server information
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(startPort);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    select(sockfd+1, &read_fds, NULL, NULL, NULL);

    if(FD_ISSET(sockfd, &read_fds)) {
        //Currently has MAXLINE so will include "trash"
        recvfrom(sockfd, (char *)buffer, MAXLINE, MSG_WAITALL, ( struct sockaddr *) &cliaddr, &clilen);
    }

    printf("Message received: %s\n", buffer);

    return 0;

    return EXIT_SUCCESS;
}