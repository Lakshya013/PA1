#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h> // included for getaddrinfo
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <pthread.h>
#include <errno.h>

#define MYPORT "4950"    // the port users will be connecting to
#define HOSTNAME "127.0.0.1"
#define MAXBUFLEN 8192

struct header_seg{
    uint32_t seq_number;
    uint32_t ack_number;
};

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//currently packet is being sent
//need to send back ack to the server, to handel packet loss
//also need to specify the rcwd

void rrecv(unsigned short int myUDPport, 
            char* destinationFile, 
            unsigned long long int writeRate) {
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXBUFLEN];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];
    struct timeval tv;


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    char UDPport[6];
    sprintf(UDPport, "%d", myUDPport);
    if ((rv = getaddrinfo(HOSTNAME, UDPport, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("listener: socket");
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind");
            continue;
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        return;
    }

    freeaddrinfo(servinfo);
    
    // Set the timeout for recvfrom
    tv.tv_sec = 5;  // 5 Seconds
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    printf("listener: waiting to recvfrom...\n");

    addr_len = sizeof their_addr;

    //save contents to a file
    FILE *fp = fopen(destinationFile, "a");
    if (fp == NULL) {
        perror("Error opening file");
    exit(1);
    }

    // Initial handshake receive
    addr_len = sizeof their_addr;
    if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0, 
        (struct sockaddr *)&their_addr, &addr_len)) == -1) {
        perror("recvfrom initial handshake");
        exit(1);
    }

    // Send back "Handshake complete" confirmation after receiving the initial packet
    memset(buf, 0, MAXBUFLEN); // Clear the buffer
    if ((numbytes = sendto(sockfd, "Handshake complete\n", 19, 0,
        (struct sockaddr *)&their_addr, addr_len)) == -1) {
        perror("Handshake Unsucessful");
    exit(1);
}

// Now proceed with the main communication loop
while(1) {
    // addr_len = sizeof their_addr;
    numbytes = recvfrom(sockfd, buf, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addr, &addr_len);
    if (numbytes == -1) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            printf("=============================================\n");
            printf("Timeout reached. No packets received for 5 seconds.\n");
            break; // Exit the loop on timeout
        } else {
            perror("recvfrom during main communication");
            exit(1);
        }
    }

    // Process the received packet
    buf[numbytes] = '\0'; // Ensure null-termination
    printf("=============================================\n");
    printf("listener: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
    struct header_seg *header = (struct header_seg*)buf;
    char *data = buf + sizeof(struct header_seg);
    printf("Seq Number: %d\n", ntohl(header->seq_number));
    printf("Ack Number: %d\n", ntohl(header->ack_number));
    printf("Data: %s\n", data);
    fprintf(fp, "%s", data); // Write data to file
         //sending reply
    memset(buf, 0, MAXBUFLEN);// clear the buffer   
    if ((numbytes = sendto(sockfd, "ACK for this packet\n", 21, 0, (struct sockaddr *)&their_addr, addr_len)) != -1) {
        printf("ACK sent\n");
    } else {
        perror("ACK not sent");
        exit(1);
    }

}

    fclose(fp);
    close(sockfd);
    printf("=============================================\n");
    printf("Socket closed\n");
    printf("=============================================\n");
    return;

}

int main(int argc, char** argv) {
    // This is a skeleton of a main function.
    // You should implement this function more completely
    // so that one can invoke the file transfer from the
    // command line.

    unsigned short int udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (unsigned short int) atoi(argv[1]);
    rrecv(udpPort, argv[2], 0);
    return 0; 
}
