#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>

#define MAXLINE 1024

typedef union {
    uint16_t opcode;
    struct {
        uint16_t opcode;
        uint8_t fnameNmode[514];
    } rq;
    struct {
        uint16_t opcode;
        uint16_t bnum;
        uint8_t data[512];
    } data;
    struct {
        uint16_t opcode;
        uint16_t bnum;
    } ack;
    struct {
        uint16_t opcode;
        uint16_t errcode;
        uint8_t errmsg[512];
    } error;
} TFTPMsg;

int startPort, endPort, alarmCount = 0;

void alarmHandler(int sig) {
    alarmCount++;
}

ssize_t tftprq(int sockfd, TFTPMsg *msg, struct sockaddr_in *cliaddr, socklen_t *clilen) {
    ssize_t c;
    if ((c = recvfrom(sockfd, msg, sizeof(*msg), 0, (struct sockaddr *) cliaddr, clilen)) < 0 && errno != EAGAIN)
        perror("TFTP: Message receive failed");
    return c;
}

ssize_t tftpdata(int sockfd, uint16_t bnum, uint8_t *data, ssize_t dlen, struct sockaddr_in *cliaddr, socklen_t clilen) {
    TFTPMsg msg;
    ssize_t c;
    msg.opcode = htons(3);
    msg.data.bnum = htons(bnum);
    memcpy(msg.data.data, data, dlen);
    if ((c = sendto(sockfd, &msg, 4 + dlen, 0, (struct sockaddr *) cliaddr, clilen)) < 0)
        perror("TFTP: Data send failed");
    return c;
}

ssize_t tftpack(int sockfd, uint16_t bnum, struct sockaddr_in *cliaddr, socklen_t clilen) {
    TFTPMsg msg;
    ssize_t c;
    msg.ack.opcode = htons(4);
    msg.ack.bnum = bnum;
    if ((c = sendto(sockfd, &msg, 6, 0, (struct sockaddr *) cliaddr, clilen)) < 0)
        perror("TFTP: Acknowledgment send failed");
    return c;
}

ssize_t tftperr(int sockfd, int errcode, char *errmsg, struct sockaddr_in *cliaddr, socklen_t clilen) {
    TFTPMsg msg;
    ssize_t c;
    msg.opcode = htons(5);
    msg.error.errcode = errcode;
    memcpy(msg.error.errmsg, errmsg, strlen(errmsg));
    if ((c = sendto(sockfd, &msg, 4 + strlen(errmsg) + 1, 0, (struct sockaddr *) cliaddr, clilen)) < 0)
        perror("TFTP: Error send failed");
    return c;
}

void tftprqHandler(TFTPMsg *msg, ssize_t len, struct sockaddr_in *cliaddr, socklen_t clilen) {
    int sockfd, opcode, done = 0;
    FILE *fd;
    struct sockaddr_in servaddr;
    char fname[512];
    TFTPMsg rmsg;
    ssize_t dlen, c = 0;
    uint16_t bnum = 0;

    // Creating new socket descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Filling new server information with new port
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(endPort);

    // Bind the socket with the new server information
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    // Parsing client request
    strcpy(fname, (char *)(msg->rq.fnameNmode + len - 10));
    if (strcmp(fname, "octet") != 0) {
        fprintf(stderr, "TFTP: Only octet mode supported");
        tftperr(sockfd, 0, "Unsupported transfer mode", cliaddr, clilen);
        exit(EXIT_FAILURE);
    }
    strcpy(fname, (char *)msg->rq.fnameNmode);
    opcode = ntohs(msg->opcode);

    // Open file descriptor with requested file name
    fd = fopen(fname, opcode == 1 ? "r" : "w");
    if (fd == NULL) {
        perror("TFTP: File open failed");
        tftperr(sockfd, errno, strerror(errno), cliaddr, clilen);
        exit(EXIT_FAILURE);
    }

    if (opcode == 1) {
        uint8_t data[512];
        while(!done) {
            dlen = fread(data, 1, sizeof(data), fd);
            bnum++;
            if (dlen < 512)
                done = 1;
            while(alarmCount != 10) {
                c = tftpdata(sockfd, bnum, data, dlen, cliaddr, clilen);
                if (c < 0) {
                    fprintf(stderr, "File transfer failed\n");
                    exit(EXIT_FAILURE);
                }
                alarm(1);
                c = tftprq(sockfd, &rmsg, cliaddr, &clilen);
                alarm(0);
                if (c >= 0 && c < 4) {
                    fprintf(stderr, "Invalid message received\n");
                    exit(EXIT_FAILURE);
                }
                if (c >= 4)
                    break;
                if (errno != EAGAIN) {
                    fprintf(stderr, "File transfer failed\n");
                    exit(EXIT_FAILURE);
                }
            }
            if (alarmCount == 10) {
                fprintf(stderr, "File transfer timed out\n");
                exit(EXIT_FAILURE);
            }
            if (ntohs(rmsg.opcode) == 5) {
                fprintf(stderr, "Error message received\n");
                exit(EXIT_FAILURE);
            }
            if (ntohs(rmsg.opcode) != 4) {
                fprintf(stderr, "Invalid message during transfer received\n");
                exit(EXIT_FAILURE);
            }
            if (ntohs(rmsg.ack.bnum) != bnum) {
                fprintf(stderr, "Invalid ack # received\n");
                tftperr(sockfd, 0, "Invalid ack # received", cliaddr, clilen);
                exit(EXIT_FAILURE);
            }
        }
    } else {
        c = tftpack(sockfd, 0, cliaddr, clilen);
        if (c < 0) {
            fprintf(stderr, "File transfer timed out\n");
            exit(EXIT_FAILURE);
        }
        while(!done) {
            while (alarmCount != 10) {
                alarm(1);
                c = tftprq(sockfd, &rmsg, cliaddr, &clilen);
                alarm(0);
                if (c >= 0 && c < 4) {
                    fprintf(stderr, "Invalid message received\n");
                    exit(EXIT_FAILURE);
                }
                if (c >= 4)
                    break;
                if (errno != EAGAIN) {
                    fprintf(stderr, "File transfer failed\n");
                    exit(EXIT_FAILURE);
                }
            }
            if (alarmCount == 10) {
                fprintf(stderr, "File transfer timed out\n");
                exit(EXIT_FAILURE);
            }
            if (ntohs(rmsg.opcode) == 5) {
                fprintf(stderr, "Error message received\n");
                exit(EXIT_FAILURE);
            }
            if (ntohs(rmsg.opcode) != 3) {
                fprintf(stderr, "Invalid message during transfer received\n");
                exit(EXIT_FAILURE);
            }
            bnum++;
            if (ntohs(rmsg.ack.bnum) != bnum) {
                fprintf(stderr, "Invalid block # received\n");
                tftperr(sockfd, 0, "Invalid block # received", cliaddr, clilen);
                exit(EXIT_FAILURE);
            }
            if (c < 512)
                done = 1;
        }
    }
}

int main(int argc, char** argv){
	int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    ssize_t len;
    TFTPMsg msg;
    uint16_t opcode;

    bzero(&servaddr, sizeof(servaddr));
    bzero(&cliaddr, sizeof(cliaddr));

    if(argc != 3){
        fprintf(stderr, "Not correct amount of arguments\n");
        exit(EXIT_FAILURE);
    }

    startPort = atoi(argv[1]);
    endPort = atoi(argv[2]);

    if(startPort > endPort){
        fprintf(stderr, "Start port is before End port\n");
        exit(EXIT_FAILURE);
    }

    // Creating socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Filling server information
    servaddr.sin_family    = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(startPort);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }

    signal(SIGALRM, alarmHandler);

    while (1) {
        if ((len = tftprq(sockfd, &msg, &cliaddr, &clilen)) < 0)
            continue;
        if (len < 4) {
            fprintf(stderr, "Invalid request size received\n");
            tftperr(sockfd, 0, "Invalid request size", &cliaddr, clilen);
            continue;
        }
        opcode = ntohs(msg.opcode);
        if (opcode == 1 || opcode == 2) {
            if (fork() == 0) {
                tftprqHandler(&msg, len, &cliaddr, clilen);
                exit(EXIT_SUCCESS);
            }
        } else {
            fprintf(stderr, "Invalid opcode request received\n");
            tftperr(sockfd, 0, "Invalid opcode request", &cliaddr, clilen);
        }
    }

    return EXIT_SUCCESS;
}