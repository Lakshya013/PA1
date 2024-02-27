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
#include <limits.h>
#include <sys/time.h>

#define BUFFER_SIZE 8192
#define TIMEOUT 1
#define DATA_LEN 2000
#define INITIAL_SIZE 2

unsigned long long int byte_sent = 0;
unsigned long long int packet_in_total = 0; 

struct header_seg{
    uint64_t seq_number;
    uint64_t ack_number;
    uint64_t data_len;
    char data[DATA_LEN];
};

struct header_seg *packet_window;
int *ack_received;

int cwnd = 1;
int ssthresh = INT_MAX;

void *get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void send_data(int sockfd, struct addrinfo *p, char *filename, unsigned long long int ToTransfer) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    unsigned long long int totalBytes = 0;

    while(totalBytes < ToTransfer){
        for(int i = 0; i < 5 ; i++){

            if(totalBytes + bytesToTransfer > ToTransfer){
                bytesToTransfer = ToTransfer - totalBytes;
            }

            char packet[sizeof(struct header_seg) + bytesToTransfer + 1]; // +1 for null-terminator

            struct header_seg *header = (struct header_seg*)packet;
            header->seq_number = htonl(bytesToTransfer);
            header->ack_number = htonl(i);

            char *buffer = packet + sizeof(struct header_seg);
            size_t bytesRead = fread(buffer, 1, bytesToTransfer, file);
            buffer[bytesRead] = '\0'; // Ensure null-termination

            if(bytesRead != bytesToTransfer){
                if(feof(file)){
                    printf("End of file reached\n");
                    exit(1);
                }
                perror("Error reading file");
                return NULL;
            }

            if(sendto(sockfd, packet, sizeof(struct header_seg) + bytesRead, 0, p->ai_addr, p->ai_addrlen) == -1){
                perror("sendto");
                continue;
            }
            totalBytes += bytesRead;
            if(bytesToTransfer == 1){
                bytesToTransfer++;
            }
            else{
                bytesToTransfer *= bytesToTransfer;
            }
            for(int i = 0; i < cwnd && i < packet_in_total; i++){
                int k = i;
                while(ack_received[k] != 0 && k < packet_in_total){k++;}
                    if(sendto(sockfd, (char*)&packet_window[k], sizeof(struct header_seg), 0, (struct sockaddr *)&their_addr, addr_len) == -1){
                        perror("sendto");
                        exit(EXIT_FAILURE);
                    }
                i = k;    
            }
            continue;
        }
        struct header_seg *header = (struct header_seg*)buffer;
        if(ack_received[ntohl(header->ack_number)]){
            // Duplicate ack
            ssthresh = cwnd / 2;
            cwnd = ssthresh + 3;
        }
        ack_received[ntohl(header->ack_number)] = 1;
        if(cwnd < ssthresh){
            cwnd *= 2;
        } else {
            cwnd += 1/cwnd;
        }
    }
    printf("File transfer complete\n");
        //packet loss
}
    


// Rest of the code remains the same

int connect_udp(int sockfd, struct addrinfo *p){
    int n;
    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    for(int attempts = 0; attempts < 5; attempts++) {
        if ((n = sendto(sockfd, "Hello, server!", 14, 0, p->ai_addr, p->ai_addrlen)) == -1) {
            perror("sendto");
            sleep(0.001); // Wait before trying again
            continue;
        }
        char buffer[BUFFER_SIZE];
        bzero(buffer, BUFFER_SIZE);
        if ((n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
            perror("recvfrom");
            sleep(0.01); // Wait before trying again
            continue;
        }
        printf("Received: %s\n", buffer);
        return 0; // Connection established
    }
    return -1; // Failed to establish connection after 5 attempts
}

/*
  You will use UDP to implement your own version of TCP. 
  Your implementation must be able to tolerate packet drops, 
  allow other concurrent connections a fair chance, and must 
  not be overly nice to other connections (should not give up the 
  entire bandwidth to other connections).
*/

/*
  This function should transfer the first bytesToTransfer bytes of
  filename to the receiver at hostname:hostUDPport correctly and
  efficiently, even if the network drops or reorders
*/
void rsend(char* hostname, 
            unsigned short int hostUDPport, 
            char* filename, 
            unsigned long long int bytesToTransfer) 
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int numbytes;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    char port[6]; // Maximum 5 digits for port number + null terminator
    sprintf(port, "%d", hostUDPport);

    if((rv = getaddrinfo(hostname, port, &hints, &servinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return ;
    }

    for(p = servinfo; p != NULL; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
            perror("talker: socket");
            continue;
        }
        if(connect_udp(sockfd, p)){
            close(sockfd);
            continue;
        }
        break;
    }

    pthread_t send_thread, recv_thread; // 
    pthread_mutex_init(&lock, NULL);

    struct send_data_args *args = malloc(sizeof(struct send_data_args));
    args->p = p;
    args->sockfd = sockfd;
    args->filename = filename;
    args->ToTransfer = bytesToTransfer;

    pthread_create(&send_thread, NULL, send_data, (void*)args);
    //pthread_create(&recv_thread, NULL, recv_data, (void*)p);

    pthread_join(send_thread, NULL);
    //pthread_join(recv_thread, NULL);
    
    pthread_mutex_destroy(&lock);
    
    // if(p == NULL){
    //     fprintf(stderr, "talker: failed to bind socket\n");
    //     return 2;
    // }

    // FILE *file = fopen(filename,"r");
    // if(file == NULL){
    //     fprintf(stderr, "File not found\n");
    //     return;
    // }

    // char* buffer = malloc(bytesToTransfer);
    // if(buffer == NULL){
    //     free(buffer);
    //     fprintf(stderr, "Memory allocation failed\n");
    //     return;
    // }

    // size_t bytesRead = fread(buffer, 1, bytesToTransfer, file);
    // if(bytesRead != bytesToTransfer){
    //     if (feof(file)) {
    //         printf("End of file reached after reading %zu bytes.\n", bytesRead);
    //     } else if (ferror(file)) {
    //         perror("Error reading file");
    //     }
    // }
    // flcose(file);

    // if(numbytes = sendto(sockfd, buffer, bytesToTransfer, 0, p->ai_addr, p->ai_addrlen) == -1){
    //     perror("talker: sendto");
    //     exit(1);
    // }

    // printf("sender: sent %d bytes to %s\n", numbytes, hostname);
    freeaddrinfo(servinfo);
    close(sockfd);
}

int main(int argc, char** argv) {
    // This is a skeleton of a main function.
    // You should implement this function more completely
    // so that one can invoke the file transfer from the
    // command line.
    int hostUDPport;
    unsigned long long int bytesToTransfer;
    char* hostname = NULL;
    char* filename = NULL;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    hostUDPport = (unsigned short int) atoi(argv[2]);
    hostname = argv[1];
    bytesToTransfer = atoll(argv[4]);
    filename = argv[3];
    rsend(hostname, hostUDPport, filename, bytesToTransfer);
    return (EXIT_SUCCESS);
}