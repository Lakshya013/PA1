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
#define DATA_LEN 2000

int counter = 0;
int total = 0;

struct header_seg{
    uint64_t seq_number;
    uint64_t ack_number;
    uint64_t data_len;
    char data[DATA_LEN];
};


void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

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
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) == -1) {
        perror("setsockopt");
        exit(1);
    }

    printf("listener: waiting to recvfrom...\n");
    while(1){

        addr_len = sizeof their_addr;
        int numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1, 0, (struct sockaddr *)&their_addr, &addr_len);
        if (numbytes == -1) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                printf("Timeout reached. No packets received for 5 seconds.\n");
                printf("Connection closed.\n");
                break; // Exit the loop on timeout
            } else {
                perror("recvfrom");
                exit(1);
            }
        }

        printf("listener: got packet from %s\n",
            inet_ntop(their_addr.ss_family,
                get_in_addr((struct sockaddr *)&their_addr),
                s, sizeof s));
        //print the message
        printf("=============================================\n");
        struct header_seg *header = (struct header_seg*)buf;
        char data[DATA_LEN];
        memcpy(data, header->data, DATA_LEN);
        //sequency number of handshake packet usally >> 10000
        if(ntohl(header->seq_number) > 100000){
            printf("%s \n", buf);
            memset(buf, 0, MAXBUFLEN);
            if ((numbytes = sendto(sockfd, "Recived Boss!", 13, 0,
                (struct sockaddr *)&their_addr, addr_len)) == -1) {
                perror("talker: sendto");
                exit(1);
            }
        }
        else{
            
            printf("Seq Number: %d\n", ntohl(header->seq_number));
            printf("Ack Number: %d\n", ntohl(header->ack_number));
            printf("Data Length: %d\n", ntohl(header->data_len));
            printf("Data: %s\n", data);

            char packet[sizeof(struct header_seg)]; // +1 for null-terminator

            struct header_seg *header_2 = (struct header_seg*)packet;
            header_2->seq_number = ((header->seq_number));
            header_2->ack_number = ((header->ack_number));
            header_2->data_len = ((header->data_len));  
            
            strncpy(header_2->data, "Received!", DATA_LEN);

            //sending reply
            if(ntohl(header->ack_number) == counter ){
                total += strlen(data);
                counter++;
                FILE *file = fopen("src/data.txt", "a+");
                if(file == NULL){
                    printf("Error opening file!\n");
                    exit(1);
                }
                fputs(data, file);
                fclose(file);
            }
            if((numbytes = sendto(sockfd, packet, sizeof(struct header_seg), 0,
                (struct sockaddr *)&their_addr, addr_len)) == -1) {
                perror("talker: sendto");
                exit(1);
            }
            memset(buf, 0, MAXBUFLEN);// clear the buffer 
        }
        printf("=============================================\n");
    //sending reply
    }
    close(sockfd);
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