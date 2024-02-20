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

#define BUFFER_SIZE 8192
#define WINDOW_SIZE 10
#define TIMEOUT 1

pthread_mutex_t lock;
int ack_received[WINDOW_SIZE] = {0}; // will work as sliding window 

struct send_data_args{
    struct addrinfo *p;
    int sockfd;
    char *filename;
    unsigned long long int ToTransfer;
};

struct header_seg{
    uint32_t seq_number;
    uint32_t ack_number;
    
    // uint32_t header_len:2;
    // uint32_t padding:2;
    // uint32_t conges:1;
    // uint32_t e_cong:1;
    // uint32_t urg:1;
    // uint32_t ack:1;
    // uint32_t push:1;
    // uint32_t reset:1;
    // uint32_t syn:1;
    // uint32_t fin:1;
    // uint32_t recieve_window:16;
};

void *get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void *send_data(void *arg){
    struct send_data_args *args = (struct send_data_args*)arg;
    struct addrinfo *p = args->p;
    unsigned long long int bytesToTransfer = 1;
    size_t ToTransfer = args->ToTransfer;
    int sockfd = args->sockfd;

    FILE *file = fopen(args->filename,"r");
    if(file == NULL){
        fprintf(stderr, "File not found\n");
        return NULL;
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
            sleep(1); // Wait before trying again
        }
    }
    fclose(file);
    return NULL;
}
// void recv_data(void *arg, int sockfd){
//     struct addrinfo *p = (struct addrinfo*) arg;
//     struct sockaddr_storage their_addr;
//     socklen_t addr_len;

// }

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
            sleep(0.001); // Wait before trying again
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
    printf("Handshake complete\n");

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