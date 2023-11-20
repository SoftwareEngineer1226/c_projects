#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include "common.h"

#define MY_EOF 1000

char **parse_args(int argc, char **argv);
verb check_args(char **args);

typedef struct {
    char inputBuffer[BUFSIZ];
    char *pNext;
    size_t bytes_left;
    int sock;
    bool end;
} ReadState;

void send_all(char* buffer, size_t size, int sock) {
    size_t bytes_sent = 0;
    do
    {
        int count;
        count = send(sock, &buffer[bytes_sent], size - bytes_sent, 0);
        if(count <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            
            print_connection_closed();
            exit(1);
        }
        bytes_sent += count;
    } while (bytes_sent < size);
}

void start_read(ReadState* state, char* pBuffer, size_t bytes_left, int sock) {
    memcpy(state->inputBuffer, pBuffer, bytes_left);
    state->pNext = state->inputBuffer;
    state->bytes_left = bytes_left;
    state->sock = sock;
    state->end = false;
}

int read_next(ReadState* state) {
    if(state->end)
        return MY_EOF;
    if(state->bytes_left > 0) {
        char c = *(state->pNext++);
        state->bytes_left--;
        return c;
    } else {
        int count =0;
        do {
            count = read(state->sock, state->inputBuffer, BUFSIZ);
            if(count == 0) {
                state->end = true;
                return MY_EOF;
            }
            if(count == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                print_error_message("read failed");
                print_connection_closed();
                exit(1);
            }
        } while(count <= 0);
        state->bytes_left = count;
        state->pNext = state->inputBuffer;
        return read_next(state);
    }
}

void read_line(ReadState* state, char *pBuffer, bool formatError) {
    int c;
    while((c = read_next(state)) != MY_EOF) {
        
        if(c == '\n' || c == '\0') {
            *pBuffer++ = '\0';
            return;
        }
        *pBuffer++ = (char)c;
    }
    if(formatError)
        print_invalid_response();
    else
        print_too_little_data();
    exit(1);
}

size_t read_size(ReadState* state) {
    size_t result;
    char* p = (char*)(&result);
    for(size_t i=0; i<sizeof(size_t); i++) {
        int c = read_next(state);
        if(c == MY_EOF) {
            print_invalid_response();
            exit(1);
        }
        *p++ = (char)c;
    }
    return result;
}

void handle_list_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[BUFSIZ];
    read_line(&state, buffer, true);
    if(strcmp(buffer, "ERROR") == 0) {
        read_line(&state, buffer, true);
        print_error_message(buffer);
        exit(1);
    }
    if(strcmp(buffer, "OK") != 0) {
        print_invalid_response();
        exit(1);
    }
    size_t size = read_size(&state);
    size_t count = 0;
    do
    {
        int c = read_next(&state);
        if(c == MY_EOF) {
            print_too_little_data();
            exit(1);
        }
        putchar(c);
        count++;
    } while (count < size);
    int c = read_next(&state);
    if(c != MY_EOF && c != 0) {
        print_received_too_much_data();
        exit(1);
    }
}

void handle_delete_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[BUFSIZ];
    read_line(&state, buffer, true);
    if(strcmp(buffer, "ERROR") == 0) {
        read_line(&state, buffer, true);
        print_error_message(buffer);
        exit(1);
    }
    if(strcmp(buffer, "OK") != 0) {
        print_invalid_response();
        exit(1);
    }
    int c = read_next(&state);
    if(c != MY_EOF && c != 0) {
        print_received_too_much_data();
        exit(1);
    }
}

void handle_get_response(char* pBuffer, size_t bytes_left, int sock, char* filename) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[BUFSIZ];
    read_line(&state, buffer, true);
    if(strncmp(buffer, "ERROR", 6) == 0) {
        read_line(&state, buffer, true);
        print_error_message(buffer);
        exit(1);
    }
    if(strncmp(buffer, "OK", 3) != 0) {
        print_invalid_response();
        exit(1);
    }
    FILE* output = fopen(filename, "wb");
    if(output == NULL) {
        printf("Can't open %s for writing\n", filename);
        exit(1);
    }
    size_t size = read_size(&state);
    size_t count = 0;
    do
    {
        int c = read_next(&state);
        if(c == MY_EOF) {
            print_too_little_data();
            exit(1);
        }
        fputc(c, output);
        count++;
    } while (count < size);
    int c = read_next(&state);
    if(c != MY_EOF && c != 0) {
        print_received_too_much_data();
        exit(1);
    }
}
void handle_put_response(char* pBuffer, size_t bytes_left, int sock) {
    ReadState state;
    start_read(&state, pBuffer, bytes_left, sock);
    char buffer[BUFSIZ];
    read_line(&state, buffer, true);
    if(strncmp(buffer, "ERROR", 6) == 0) {
        read_line(&state, buffer, true);
        print_error_message(buffer);
        exit(1);
    }
    if(strncmp(buffer, "OK", 3) != 0) {
        print_invalid_response();
        exit(1);
    }
    int c = read_next(&state);
    if(c != MY_EOF && c != 0) {
        print_received_too_much_data();
        exit(1);
    }
}



typedef struct s_response{
    int status;
    char* error_message;
    size_t size;
}s_response;

s_response* parse_server_response(char* buffer, size_t* off){

    while(buffer[*off] != '\n'){
        (*off)++;
    }

    char message[(*off)];
    memcpy(message, buffer, (*off));

    if(strcmp(message, "OK") != 0 && strcmp(message, "ERROR") != 0){
        print_invalid_response();
        return NULL;
    }

    s_response* serv_resp = (s_response*)malloc(sizeof(s_response));

    if(strcmp(message, "OK") == 0){
        serv_resp->status = OK;
    }
    else if(strcmp(message, "ERROR") == 0){
        serv_resp->status = ERROR;

        *off += 1;
        size_t error_off = 0;
        while(buffer[(*off)+error_off] != '\n'){
            error_off++;
        }
        serv_resp->error_message = (char*)malloc(error_off);

        memcpy(serv_resp->error_message, buffer + (*off), error_off);

        *off += error_off+1;
        
    }
    else{

        print_invalid_response();
        free(serv_resp);
        return NULL;
    }
    return serv_resp;
}

void printBuffer(const char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X ", (unsigned char)buffer[i]); // Prints in hexadecimal format
    }
    printf("\n");
}

void create_message(char* buffer, char* verb_as_char, char* filename){
    if(filename != NULL && strlen(filename) > 0)
        sprintf(buffer, "%s %s\n", verb_as_char, filename);
    else
        sprintf(buffer, "%s\n", verb_as_char);
}

void send_file(char* filename, int sock) {
    struct stat file_info;
    stat(filename, &file_info);
    FILE* f = fopen(filename, "rb");
    if(f == NULL) {
        printf("Can't open file %s\n", filename);
        exit(1);
    }
    size_t filesize = file_info.st_size;
    if(send(sock, &filesize, sizeof(size_t), 0) <= 0) {
        printf("send failed\n");
    }

    size_t bytes_written = 0;
    char buffer[BUFSIZ];
    do
    {
        size_t count = fread(buffer, 1, BUFSIZ, f);
        if(count == 0 || ferror(f)) {
            printf("error reading file");
            fclose(f);
            exit(1);
        }
        bytes_written += count;
        send_all(buffer, count, sock);
    } while (bytes_written < filesize);
    fclose(f);
}

/* reading waiting errors on the socket
 * return 0 if there's no, 1 otherwise
 */
static int socket_check(int fd)
{
   int ret;
   int code;
   socklen_t len = sizeof(int);

   ret = getsockopt(fd, SOL_SOCKET, SO_ERROR, &code, &len);

   if ((ret || code)!= 0)
      return 1;

   return 0;
}

/* create a TCP socket with non blocking options and connect it to the target
* if succeed, add the socket in the epoll list and exit with 0
*/
static int create_and_connect( struct sockaddr_in target , int epfd)
{
   int yes = 1;
   int sock;

   // epoll mask that contain the list of epoll events attached to a network socket
   static struct epoll_event Edgvent;


   if( (sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
   {
      perror("socket");
      exit(1);
   }

   // set socket to non blocking and allow port reuse
   if ( (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) < 0 ||
      (fcntl(sock, F_SETFL, O_NONBLOCK)) == -1) )
   {
      perror("setsockopt || fcntl");
      exit(1);
   }

   if( connect(sock, (struct sockaddr *)&target, sizeof(struct sockaddr)) == -1
      && errno != EINPROGRESS)
   {
      // connect doesn't work, are we running out of available ports ? if yes, destruct the socket
      if (errno == EAGAIN)
      {
         perror("connect is EAGAIN");
         close(sock);
         exit(1);
      }
   }
   else
   {
      /* epoll will wake up for the following events :
       *
       * EPOLLIN : The associated file is available for read(2) operations.
       *
       * EPOLLOUT : The associated file is available for write(2) operations.
       *
       * EPOLLRDHUP : Stream socket peer closed connection, or shut down writing 
       * half of connection. (This flag is especially useful for writing simple 
       * code to detect peer shutdown when using Edge Triggered monitoring.)
       *
       * EPOLLERR : Error condition happened on the associated file descriptor. 
       * epoll_wait(2) will always wait for this event; it is not necessary to set it in events.
       */
      Edgvent.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET ;
      //Edgvent.events = EPOLLOUT | EPOLLIN | EPOLLRDHUP | EPOLLERR;

      Edgvent.data.fd = sock;

      // add the socket to the epoll file descriptors
      if(epoll_ctl(epfd, EPOLL_CTL_ADD, sock, &Edgvent) != 0)
      {
         perror("epoll_ctl, adding socket\n");
         exit(1);
      }
   }

   return 0;
}

int main(int argc, char **argv) {
    char** args = parse_args(argc, argv);
    if(args == NULL){
        print_client_help();
        return -1;
    }
    char* host = args[0];
    int port = atoi(args[1]);
    char* verb_as_char = args[2];
    verb _verb = check_args(args);
    char* firstFile = NULL;
    char* secondFile = NULL;
    if(argc > 3){
        firstFile = args[3];
        secondFile = args[3];
    }
    if(argc > 4){
        secondFile = args[4];
    }

    // the epoll file descriptor
    int epfd;

    // epoll structure that will contain the current network socket and event when epoll wakes up
    static struct epoll_event *events;
    static struct epoll_event event_mask;

    //int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFSIZ] = {0};

    int maxconn = 1;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
        print_error_message("Invalid address/ Address not supported");
        return -1;
    }

    // create the special epoll file descriptor
    epfd = epoll_create(maxconn);

    // allocate enough memory to store all the events in the "events" structure
    if (NULL == (events = calloc(maxconn, sizeof(struct epoll_event))))
    {
        perror("calloc events");
        exit(1);
    };

    if(create_and_connect(serv_addr, epfd) != 0)
    {
        perror("create and connect");
        exit(1);
    }

    int count, i;

    do
   {
      /* wait for events on the file descriptors added into epfd
       *
       * if one of the socket that's contained into epfd is available for reading, writing,
       * is closed or have an error, this socket will be return in events[i].data.fd
       * and events[i].events will be set to the corresponding event
       *
       * count contain the number of returned events
       */
        count = epoll_wait(epfd, events, maxconn, 1000);
        for(i=0;i<count;i++)
        {
            if (events[i].events & EPOLLOUT) //socket is ready for writing
            {
                // verify the socket is connected and doesn't return an error
                if(socket_check(events[i].data.fd) != 0)
                {
                   perror("write socket_check");
                   close(events[i].data.fd);
                   continue;
                }
                else
                {
                    create_message(buffer, verb_as_char, firstFile);
                    send_all(buffer, strlen(buffer), events[i].data.fd);
                    if(_verb == PUT){
                        send_file(secondFile, events[i].data.fd);
                    }
                    
                    /* we just wrote on this socket, we don't want to write on it anymore
                    * but we still want to read on it, so we modify the event mask to
                    * remove EPOLLOUT from the events list
                    */
                    event_mask.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
                    event_mask.data.fd = events[i].data.fd;

                    if(epoll_ctl(epfd, EPOLL_CTL_MOD, events[i].data.fd, &event_mask) != 0)
                    {
                        perror("epoll_ctl, modify socket\n");
                        exit(1);
                    }
                }
            }
        
            if (events[i].events & EPOLLIN) //socket is ready for writing
            {
                // verify the socket is connected and doesn't return an error
                if(socket_check(events[i].data.fd) != 0)
                {
                   perror("read socket_check");
                   close(events[i].data.fd);
                   continue;
                }
                else 
                {
                    // Receiving the message from the server
                    int recvCount = read(events[i].data.fd, buffer, BUFSIZ); 
                    if ( recvCount < 0) {
                        print_error_message("Read error");
                        close(events[i].data.fd);
                        return -1;
                    }

                    if(_verb == LIST) {
                        handle_list_response(buffer, recvCount, events[i].data.fd);
                        close(events[i].data.fd);
                        exit(0);
                    }
                    if(_verb == DELETE) {
                        handle_delete_response(buffer, recvCount, events[i].data.fd);
                        print_success();
                        close(events[i].data.fd);
                        exit(0);
                    }
                    if(_verb == GET) {
                        handle_get_response(buffer, recvCount, events[i].data.fd, secondFile);
                        close(events[i].data.fd);
                        exit(0);
                    }
                    if(_verb == PUT) {
                        handle_put_response(buffer, recvCount, events[i].data.fd);
                        print_success();
                        close(events[i].data.fd);
                        exit(0);
                    }
                }
            }
            if (events[i].events & (EPOLLRDHUP | EPOLLHUP)) //socket closed
            {
                // socket is closed, remove the socket from epoll and create a new one
                epoll_ctl(epfd, EPOLL_CTL_DEL, events[i].data.fd, NULL);

                if(close(events[i].data.fd)!=0)
                {
                   perror("close");
                   continue;
                }
            }

            if (events[i].events & EPOLLERR)
            {
                perror("epoll");
                continue;
            }
        }

    } while(1);

    print_client_usage();
    return 1;
}

/**
 * Given commandline argc and argv, parses argv.
 *
 * argc argc from main()
 * argv argv from main()
 *
 * Returns char* array in form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char **parse_args(int argc, char **argv) {
    if (argc < 3) {
        return NULL;
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL) {
        return NULL;
    }

    char **args = (char**)calloc(1, 6 * sizeof(char *));
    args[0] = host;
    args[1] = port;
    args[2] = argv[2];
    char *temp = args[2];
    while (*temp) {
        *temp = toupper((unsigned char)*temp);
        temp++;
    }
    if (argc > 3) {
        args[3] = argv[3];
    }
    if (argc > 4) {
        args[4] = argv[4];
    }

    return args;
}

/**
 * Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * args     arguments to parse
 *
 * Returns a verb which corresponds to the request method
 */
verb check_args(char **args) {
    if (args == NULL) {
        print_client_usage();
        exit(1);
    }

    char *command = args[2];

    if (strcmp(command, "LIST") == 0) {
        return LIST;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL) {
            return GET;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
            return DELETE;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL ) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}
