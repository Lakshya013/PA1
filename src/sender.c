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

    unsigned long long int bytesRemaining = ToTransfer; // Track remaining bytes to send
    uint64_t seq_num = 0;
    while (bytesRemaining > 0) {
        int to_send = DATA_LEN < bytesRemaining ? DATA_LEN : bytesRemaining;
        
        char buffer[sizeof(struct header_seg)];
        struct header_seg *header = (struct header_seg*)buffer;

        size_t bytes_read = fread(header->data, 1, to_send, file);
        if (bytes_read == 0) {
            if (ferror(file)) { // Check for fread error
                perror("fread");
                exit(EXIT_FAILURE);
            }
            break; // End of file reached or no data read, exit the loop
        }

        header->data[bytes_read] = '\0';

        seq_num += bytes_read; // Update sequence number based on bytes actually read

        header->seq_number = htonl(seq_num);
        // Assuming ack_number is being used correctly; otherwise, adjust as needed
        header->ack_number = htonl(0); // Placeholder, adjust based on your logic
        header->data_len = htonl(bytes_read);

        if (sendto(sockfd, buffer, sizeof(struct header_seg), 0, p->ai_addr, p->ai_addrlen) == -1) {
            perror("sendto");
            exit(EXIT_FAILURE);
        }

        bytesRemaining -= bytes_read; // Decrement remaining bytes by the amount just sent
    }

    fclose(file);
}


int check_ack(){

    for(int i = 0 ; i < packet_in_total; i++){
        if(ack_received[i] == 0){
            return 1;
        }
    }
    return 0;
}

void recv_data(int sockfd, unsigned long long int ToTransfer){
    struct sockaddr_storage their_addr;
    socklen_t addr_len;

    while(check_ack(ToTransfer)){
        char buffer[sizeof(struct header_seg)];
        if(recvfrom(sockfd, buffer, sizeof(struct header_seg), 0, (struct sockaddr *)&their_addr, &addr_len) == -1){
            perror("recvfrom");
            if(errno != EAGAIN && errno != EWOULDBLOCK){
                exit(EXIT_FAILURE);
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
    printf("Handshake complete\n");

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    byte_sent = bytesToTransfer;  

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0){
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    unsigned long long int total_packets = (bytesToTransfer + DATA_LEN - 1) / DATA_LEN;
    
    packet_window = malloc(total_packets * sizeof(struct header_seg));
    ack_received = malloc(total_packets * sizeof(int));

    send_data(sockfd, p, filename, bytesToTransfer);
    recv_data(sockfd, bytesToTransfer);

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