#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <assert.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "common.h"
#include "format.h"
#include "vector.h"

#define BASE_FOLDER "test"

#define MAXEVENTS 64

enum {
// This indicates that a stream we are reading has come to an end, and the client has stopped sending data
    STREAM_END = 1001,
// This inidcates tha a stream we are reading has no more data, but the client is still sending, however reading it would block the process
    STREAM_PENDING,
// This indicates the stream is in error
    STREAM_ERROR,
// This return code indicates everything completed without blocking
    STREAM_DONE,
    
    STREAM_OK,
};

// Struct representing a stream of data on a socket
typedef struct {
    char buffer[BUFSIZ];   // The buffer we have read in already, or if a write, the buffer we are writing out
    int socket;            // The socket this stream is using
    size_t position;       // Next position within the buffer (either the next char to be returned, or the next one to be written
    size_t bytesInBuffer;  // Total bytes that were read into the buffer, this is not needed when writing, since position serves that purpose
    bool atEnd;            // Set true if at the end of the stream (after a STREAM_END has been returned)
    bool inError;          // Set true if at the stream has an error (after STREAM_ERROR has been returned)
} Stream;

enum {
    STATUS_SESSION_WAIT = 2001, // Returned to mean session is still good but must wait for the client on the socket
    STATUS_SESSION_END,         // Returned to mean that the session has ended.
    STATUS_SESSION_ERROR,       // Returned to mean that the session had a communication error
};
    
enum {
    STATE_WRITING = 3001,
    STATE_READING_HEADER,
    STATE_SENDING_GET,
    STATE_READING_PUT,
    STATE_WRITING_LIST,
    STATE_INTERNAL_ERROR,
    STATE_DONE
};


typedef struct {
    Stream stream;          // Used to buffer both input and output from the stream. Please note we do not read and write at the same time. Always read, then write
    int state;                // Current state of the session -- what is it doing/
    char input[BUFSIZ];     // When reading the header, it is loaded in here.
    char filename[BUFSIZ];  // Filename for command
    size_t inputPos;        // When reading the header this is the next position to put the next character
    FILE* fd;                // For file based operations, GET and PUT this is the file we are reading or writing
    int status;             // Status of the session is either SESSION_WAIT meaning it is waiting for more data
                            // to read or write, SESSION_END where it is ended or SESSION_ERROR if it is in error
    char* listData;            // When we get a LIST command we have to generate the full list and buffer in memory in case ongoing commands
                            // in parallel change the list. So this is a malloced buffer containing that list
    size_t listDataPos;        // When writing out list data in multiple buffer writes, this is position we have already writeen up to
    bool reading;           // IS this session currently reading from the socket or writing to it

    size_t totalBytesForPut;
    size_t totalWritten;
} Session;

static char base_temp_dir[BUFSIZ];
static vector* directory = NULL;
static my_hash_table_t sock_to_session_hashtable;
static int verbose_flag = 0;

// Flush the rest of the write buffer to the socket, and clear it out, however, don't block. This can return STREAM_END, STREAM_PENDING, STREAM_ERROR or STREAM_OK
int Stream_Send(Stream* stream);
// Read the next character from the stream without blocking, or send back STREAM_END, STREAM_PENDING or STREAM_ERROR
int Stream_GetNext(Stream* stream);
// Reset a stream to initial state with the socket passed in
void Stream_Reset(Stream* stream, int socket);
// Return true is there is data still in the buffer unread
bool Stream_has_more(Stream* stream);

Session* Session_create(int sock);
void session_start_list(Session *session);
void session_start_get(Session* session);
void session_start_put(Session* session, size_t cmdLen);

bool continue_reading_header(Session* session);
bool continue_sending_get(Session* session);
bool continue_reading_put(Session* session);
void session_start_delete(Session *session);
bool TryParse(Session* session, char* cmd, bool hasFilename);
int Session_processNext(Session* session);
void write_short_string(Session* session, char* str);

void insert_size_into_mem(char* pBuffer, size_t size);
void send_all(char* buffer, size_t size, int sock);
void write_all(FILE* f, char* buffer, size_t size);
int remove_directory(const char *path);
int set_sighandler(sighandler_t sig_usr);
int exist_in_vector(vector* vector, char *filename);

typedef void (*sighandler_t) (int);

static void print_usage(const char* progname)
{
  fprintf(stderr, 
      "Usage: %s <port>\n"
      "\n"
      "where <options>: \n"
      " -verbose : output log\n"
      "\n"
      ,
      progname
      );
}

static void sig_usr_un(int signo)
{
  if (signo == SIGCHLD || signo == SIGPIPE)
    return;

  LOG("nbnserver: Signal %d received.\n", signo);
  
  remove_directory(base_temp_dir);
  LOG("nbnserver: Finished.\n");
  exit(0);

  return;
}

int set_sighandler(sighandler_t sig_usr)
{
  if (signal(SIGINT, sig_usr) == SIG_ERR ) {
    LOG("No SIGINT signal handler can be installed.\n");
    return -1;
  }
    
  if (signal(SIGPIPE, sig_usr) == SIG_ERR ) {
    LOG("No SIGPIPE signal handler can be installed.\n");
    return -1;
  }

  if (signal(SIGCHLD , sig_usr)  == SIG_ERR ) {
    LOG("No SIGCHLD signal handler can be installed.\n");
    return -1;
  }

  if (signal(SIGTERM , sig_usr)  == SIG_ERR ) {
    LOG("No SIGTERM signal handler can be installed.\n");
    return -1;
  }

  if (signal(SIGHUP , sig_usr)  == SIG_ERR ) {
    LOG("No SIGHUP signal handler can be installed.\n");
    return -1;
  }

  return 0;
}

int exist_in_vector(vector* vector, char *data)
{
    int ret = 0;
    VECTOR_FOR_EACH(vector, item, {
        if(!strcmp(data, item)) {
            ret = 1;
            break;
        }
    });
    return ret;
}


int remove_directory(const char *path) {
   DIR *d = opendir(path);
   size_t path_len = strlen(path);
   int r = -1;

   if (d) {
      struct dirent *p;

      r = 0;
      while (!r && (p=readdir(d))) {
          int r2 = -1;
          char *buf;
          size_t len;

          /* Skip the names "." and ".." as we don't want to recurse on them. */
          if (!strcmp(p->d_name, ".") || !strcmp(p->d_name, ".."))
             continue;

          len = path_len + strlen(p->d_name) + 2; 
          buf = malloc(len);

          if (buf) {
             struct stat statbuf;

             snprintf(buf, len, "%s/%s", path, p->d_name);
             if (!stat(buf, &statbuf)) {
                if (S_ISDIR(statbuf.st_mode))
                   r2 = remove_directory(buf);
                else
                   r2 = unlink(buf);
             }
             free(buf);
          }
          r = r2;
      }
      closedir(d);
   }

   if (!r)
      r = rmdir(path);

   return r;
}

bool TryParse(Session* session, char* cmd, bool hasFilename) {
    size_t cmdLen = strlen(cmd);
    if(strncmp(session->input, cmd, cmdLen) != 0)
        return false;

    if(hasFilename) {
        if(session->input[cmdLen] != ' ') 
            return false;
        char* nl = strchr(session->input, '\n');
        if(nl == NULL)
            return false;
        char* filenameStart = &session->input[cmdLen+1];
        strncpy(session->filename, filenameStart, nl - filenameStart);
        session->filename[nl - filenameStart] = '\0';
    }
        
    return true;
}

// Called when we get data and we are in the middle of reading the header
bool continue_reading_header(Session* session) {
    do
    {
        int c = Stream_GetNext(&session->stream);
        if(c == STREAM_END) {
            //send_short_string("Invalid request\n"); // TODO replace this with the correct string
            //session->status = STATUS_SESSION_END;
            break;
        }
        session->input[session->inputPos++] = c;
        
    } while(session->state == STATE_READING_HEADER);

    if(TryParse(session, "LIST", false))
        session_start_list(session);
    else if(TryParse(session, "GET", true))
        session_start_get(session);
    else if(TryParse(session, "PUT", true))
        session_start_put(session, 3);
    else if(TryParse(session, "DELETE", true))
        session_start_delete(session);
    
    return false;//Session_has_more(&session->stream);
}

static void sending_put_response(Session* session, char *msg)
{
    int exist = exist_in_vector(directory, session->filename);
    fclose(session->fd);
    send_all(msg, strlen(msg), session->stream.socket);
    session->state = STATE_DONE;
    session->status = STATUS_SESSION_END;
    if(!exist) {
        vector_push_back(directory, strdup(session->filename));
    }
}

bool continue_reading_put(Session* session)
{
    write_all(session->fd, session->stream.buffer, session->stream.bytesInBuffer);
    session->totalWritten += session->stream.bytesInBuffer;

    if(session->totalWritten == session->totalBytesForPut) {
        sending_put_response(session, "OK\n");
        session->state = STATE_DONE;
    } else if(session->totalWritten > session->totalBytesForPut) { // for short content
        sending_put_response(session, "ERROR\nReceived too mush data\n");
        session->state = STATE_DONE;
    }
    return false;
}

// Reset a stream to initial state with the socket passed in
void Stream_Reset(Stream* stream, int socket) {
    stream->socket = socket;
    stream->position = 0;
    stream->bytesInBuffer = 0;
    stream->atEnd = false;
    stream->inError = false;
}

// Read the next character from the stream without blocking, or send back STREAM_END, STREAM_PENDING or STREAM_ERROR
int Stream_GetNext(Stream* stream) {
    assert(! stream->atEnd && ! stream->inError); // Shouldn't be called when the stream is no longer viable
    if(stream->position < stream->bytesInBuffer) {
        return stream->buffer[stream->position++];
    }
    return STREAM_END;
}

// Write next character to buffer, and flush to stream if buffer is full. This can return STREAM_END, STREAM_PENDING, or STREAM_ERROR
int Stream_WriteNext(Stream* stream, char c) {
    assert(! stream->atEnd && ! stream->inError); // Shouldn't be called when the stream is no longer viable
    if(stream->position == BUFSIZ) {
        int result = 0;// = Stream_SendUntilBlock(stream);
        if(result != STREAM_OK)
            return result;
        assert(stream->position = 0);
        stream->buffer[stream->position++] = c;
    }
    return STREAM_END;
}


static int sending_get_response(Session* session) {
    char *filename = session->filename;
    int sock = session->stream.socket;
    
    LOG("Server get for %s\n", filename);
    char buffer[BUFSIZ];
    snprintf(buffer, sizeof(buffer), "%s/%s", base_temp_dir, filename);
    struct stat file_info;
    stat(buffer, &file_info);
    FILE* f = fopen(buffer, "rb");
    if(f == NULL) {
        sprintf(buffer, "ERROR\nUnknown file\n");
        send_all(buffer, strlen(buffer), sock);
        session->state = STATE_INTERNAL_ERROR;
        session->status = STATUS_SESSION_ERROR;
        return -1;
    }
    // Set up the header
    strcpy(buffer, "OK\n12345678"); // Note we will overwrite 12345678 with a size_t
    char *pBuffer = buffer + strlen(buffer);
    insert_size_into_mem(&buffer[3], file_info.st_size);
    send_all(buffer, pBuffer - buffer, sock);
    size_t bytes_read = 0;
    do
    {
        int count = fread(buffer, 1, BUFSIZ, f);
        if(count <= 0) {
            print_error_message("fread failed");
            fclose(f);
            session->state = STATE_INTERNAL_ERROR;
            session->status = STATUS_SESSION_ERROR;
            return -1;
        }
        bytes_read += count;
        send_all(buffer, count, sock);
    } while (bytes_read < (size_t)file_info.st_size);
    fclose(f);

    session->state = STATE_DONE;
    session->status = STATUS_SESSION_END;

    return 0;
}

void session_start_delete(Session *session)
{
    int result = 0;
    char fullpath[BUFSIZ] = "";

    if(strlen(session->filename)) {
        for(size_t i=0; i < vector_size(directory); i++) {
            char *diritem = vector_get(directory, i);
            if( !strcmp(session->filename, diritem) ) {
                snprintf(fullpath, sizeof(fullpath), "%s/%s", base_temp_dir, session->filename);
                free(diritem);
                vector_erase(directory, i); 
                break;
            }
        }
        
        char buffer[BUFSIZ] = "";
        result = unlink(fullpath);
        if(result == 0) {
            sprintf(buffer, "OK\n");
            send_all(buffer, strlen(buffer), session->stream.socket);
        } else {
            sprintf(buffer, "ERROR\n%s\n", strerror(errno));
            send_all(buffer, strlen(buffer), session->stream.socket);
        }
    }
    session->state = STATE_DONE;
    session->status = STATUS_SESSION_END;
}

void session_start_list(Session *session)
{
    char buffer[BUFSIZ];
    
    // Note we assume there is a vector called directory storing the directory of all files in the temp folder
    // We do this as per instructions rather than reading the filesystem directly.
    size_t sizeCount = 0;
    VECTOR_FOR_EACH(directory, diritem, {
        sizeCount += strlen(diritem) + 1; // +1 for the newline
    });
    sizeCount++; // Add one for the end null character;
    session->listData = calloc(1, sizeCount);
    if(session->listData == NULL) {
        print_error_message("calloc failed");
        session->state = STATE_INTERNAL_ERROR;
        session->status = STATUS_SESSION_ERROR;
        return;
    }
    
    char* end = session->listData;
    VECTOR_FOR_EACH(directory, diritem, {
        size_t len = strlen((char*)diritem);
        strcpy(end, diritem);
        end[len] = '\n';
        end += len + 1;
    });
    *end = '\0';

    // Set up the header
    strcpy(buffer, "OK\n12345678"); // Note we will overwrite 12345678 with a size_t
    char *pBuffer = buffer + strlen(buffer);
    insert_size_into_mem(&buffer[3], sizeCount);
    send_all(buffer, pBuffer - buffer, session->stream.socket);
    send_all(session->listData, sizeCount, session->stream.socket);
    free(session->listData);
    session->listData = NULL;

    session->state = STATE_DONE;
    session->status = STATUS_SESSION_END;

}

void session_start_get(Session* session) {
    if(strlen(session->filename)) {
        sending_get_response(session);
        session->state = STATE_SENDING_GET;
    }
}

void session_start_put(Session* session, size_t cmdLen) {

    if(strlen(session->filename)) { // got filename, but didn't get filesize
        LOG("Server put for %s\n", session->filename);
        
        size_t head_size = cmdLen + strlen(session->filename) + 2; // "PUT abc.png\n"
    
        if(session->inputPos >= head_size + 8) {
            char buffer[BUFSIZ] = "";
            snprintf(buffer, sizeof(buffer), "%s/%s", base_temp_dir, session->filename);
            session->fd = fopen(buffer, "wb");
            if(session->fd == NULL) {
                sprintf(buffer, "ERROR\n%s\n", strerror(errno));
                send_all(buffer, strlen(buffer), session->stream.socket);
                session->state = STATE_INTERNAL_ERROR;
                session->status = STATUS_SESSION_ERROR;
                return;
            }
            
            memcpy(&session->totalBytesForPut, &session->input[head_size], sizeof(size_t));

            size_t nowWritingBytes = session->inputPos - head_size - 8;
            if(nowWritingBytes > 0) {
                write_all(session->fd, &session->input[head_size + 8], nowWritingBytes);
                session->totalWritten = nowWritingBytes;
            }

            if(session->totalWritten == session->totalBytesForPut) { // for short content
                sending_put_response(session, "OK\n");
            } else if(session->totalWritten > session->totalBytesForPut) { // for short content
                sending_put_response(session, "ERROR\nReceived too mush data\n");
            } else {
                session->state = STATE_READING_PUT;
            }
        } 

    }
}

Session* Session_create(int sock) {
    Session* session = NULL;
    session = calloc(1, sizeof(Session));
    if(session == NULL) {
        print_error_message("calloc failed");
        return NULL;
    }
    Stream_Reset(&session->stream, sock);
    memset(session->input, 0, BUFSIZ);
    memset(session->filename, 0, BUFSIZ);
    session->state = STATE_READING_HEADER;
    session->inputPos = 0;
    session->fd = NULL;
    session->status = STATUS_SESSION_WAIT;
    session->listData = NULL;
    session->listDataPos = 0;
    session->reading = true;
    return session;
}

int Session_processNext(Session* session) {
    bool running = false;
    do
    {
        switch(session->state) {
            case STATE_WRITING:
                //running = continue_writing(session);
                break;
            case STATE_READING_HEADER:
                running = continue_reading_header(session);
                break;
            case STATE_SENDING_GET: 
                //running = continue_sending_get(session);
                break;
            case STATE_READING_PUT:
                running = continue_reading_put(session);
                break;
            case STATE_WRITING_LIST:
                //running = continue_writing_list(session);
                break;
            case STATE_INTERNAL_ERROR:
                running = false;
                break;
            case STATE_DONE:
                session->status = STATUS_SESSION_END;
                break;
            default:
                assert(false); //Should never be in anything except the above states.
        }
    }while(running);
    return session->status;
}

static int
make_socket_non_blocking (int sfd)
{
    int flags, s;

    flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1)
    {
        perror ("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl (sfd, F_SETFL, flags);
    if (s == -1)
    {
        perror ("fcntl");
        return -1;
    }

    return 0;
}

static int
create_and_bind (int portNum)
{

    int sfd;

    struct sockaddr_in address;

    // Creating socket file descriptor
    if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
      perror("Socket creation failed");
      exit(EXIT_FAILURE);
    }

    int enable = 1;
    if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))<0) {
        perror(NULL);
        exit(1);
    }
    if(setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int))<0) {
        perror(NULL);
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(portNum);

    // Binding to the specified port
    if (bind(sfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        print_error_message("Bind failed");
        exit(EXIT_FAILURE);
    }

    return sfd;
}


void printBuffer(const char *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
        fprintf(stderr, "%02X ", (unsigned char)buffer[i]); // Prints in hexadecimal format
    }
    fprintf(stderr, "\n");
}

void send_all(char* buffer, size_t size, int sock) {
    size_t bytes_sent = 0;
    do
    {
        int count;
        count = send(sock, &buffer[bytes_sent], size - bytes_sent, 0);
        if(count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            
            print_error_message("Client socket:");
            return;
        }
        if(count == 0) {
            print_error_message("Client disconnected\n");
            return;
        }
        bytes_sent += count;
    } while (bytes_sent < size);
}

void write_all(FILE* f, char* buffer, size_t size) {
    size_t bytes_sent = 0;
    do
    {
        int count;
        count = fwrite(&buffer[bytes_sent], 1, size - bytes_sent, f);
        if(count < 0) {
            print_error_message("Output file");
            return;
        }
        if(count == 0) {
            print_error_message("Output write failed");
            return;
        }
        bytes_sent += count;
    } while (bytes_sent < size);
}

void insert_size_into_mem(char* pBuffer, size_t size) {
    memcpy(pBuffer, &size, sizeof(size_t));
}

static void initialize()
{
    char base_path[] = "XXXXXX";

    char *tmp_dir = mkdtemp(base_path);
    if(tmp_dir == NULL) {
        print_error_message("mkdtemp faild");
        exit(EXIT_FAILURE);
    }
    print_temp_directory(tmp_dir);
    
    snprintf(base_temp_dir, sizeof(base_temp_dir), "%s", tmp_dir);

    fprintf(stderr, "Storing files at '%s'\n", base_temp_dir);

    hashtable_ts_init(&sock_to_session_hashtable, NULL, "sock_to_session_hashtable");

    directory = vector_create(NULL, NULL, NULL);
}
static int parse_args(int argc, char* argv[])
{
  int i;
  for(i=2; i<argc; i++){

    char* arg = argv[i];

    if(!strcmp(arg, "--verbose")) {
        verbose_flag = 1;
    } else {
      	fprintf(stderr, "%s: unknown parameter '%s'\n",argv[0],arg);
      print_usage(argv[0]);
      return -1;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
    if(argc < 2) {
        print_usage(argv[0]);
        exit(-1);
    }
    int portNum = atoi(argv[1]);
    if(portNum < 1024) {
        print_usage(argv[0]);
        exit(-1);
    }

    if(parse_args(argc, argv))
        return -1;

    if(set_sighandler(sig_usr_un))
            return -1;

    int efd;
    struct epoll_event event;
    struct epoll_event *events = NULL;
    int server_fd, s;

    initialize();

    fprintf(stderr, "Listening on port %d\n\n", portNum);

    server_fd = create_and_bind (portNum);
    if (server_fd == -1)
      exit(EXIT_FAILURE);

    s = make_socket_non_blocking (server_fd);
    if (s == -1)
      exit(EXIT_FAILURE);

    // Listening for incoming connections
    s = listen (server_fd, 3);
    if (s == -1) {
      print_error_message("Listen failed");
      exit(EXIT_FAILURE);
    }


    efd = epoll_create1 (0);
    if (efd == -1) {
      print_error_message("epoll_create failed");
      exit(EXIT_FAILURE);
    }

    event.data.fd = server_fd;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl (efd, EPOLL_CTL_ADD, server_fd, &event);
    if (s == -1) {
      print_error_message("epoll_ctl failed");
      exit(EXIT_FAILURE);
    }

    /* Buffer where events are returned */
    events = calloc (MAXEVENTS, sizeof event);

    /* The event loop */
    while (1) {
        int n, i;

        n = epoll_wait (efd, events, MAXEVENTS, -1);
        for (i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN) && !(events[i].events & EPOLLOUT)))
            {
              print_error_message ("epoll error");
              close (events[i].data.fd);
              continue;
            }

            else if (server_fd == events[i].data.fd)
            {
                /* We have a notification on the listening socket, which
                 means one or more incoming connections. */
                while (1) {
                    struct sockaddr in_addr;
                    socklen_t in_len;
                    int infd;
                    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

                    in_len = sizeof in_addr;
                    infd = accept (server_fd, &in_addr, &in_len);
                    if (infd == -1) {
                        if ((errno == EAGAIN) ||
                              (errno == EWOULDBLOCK))
                        {
                            /* We have processed all incoming
                               connections. */
                            break;
                        }
                        else
                        {
                            print_error_message ("accept failed");
                            break;
                        }
                    }

                    s = getnameinfo (&in_addr, in_len,
                                     hbuf, sizeof hbuf,
                                     sbuf, sizeof sbuf,
                                     NI_NUMERICHOST | NI_NUMERICSERV);
                    if (s == 0) {
                      LOG("Accepted connection on descriptor %d "
                             "(host=%s, port=%s)\n", infd, hbuf, sbuf);
                    }

                    /* Make the incoming socket non-blocking and add it to the
                       list of fds to monitor. */
                    s = make_socket_non_blocking (infd);
                    if (s == -1)
                      exit(EXIT_FAILURE);

                    event.data.fd = infd;
                    event.events = EPOLLIN | EPOLLET;
                    s = epoll_ctl (efd, EPOLL_CTL_ADD, infd, &event);
                    if (s == -1) {
                      print_error_message ("epoll_ctl failed");
                      exit(EXIT_FAILURE);
                    }
                }
                continue;
            }
            else {
                if(events[i].events & EPOLLIN) {
                    /* We have data on the fd waiting to be read. Read and
                    display it. We must read whatever data is available
                     completely, as we are running in edge-triggered mode
                     and won't get a notification again for the same
                     data. */
                    int done = 0;
                    Session* session = NULL;

                    while (1)
                    {
                        ssize_t bytesRead;
                        char *buffer;

                        const uint32_t keyP = events[i].data.fd;
                        hashtable_rc_t hash_rc0;
                        hash_rc0 = hashtable_ts_get(&sock_to_session_hashtable, keyP, (void * *)&session);
                        if (hash_rc0 != HASH_TABLE_OK) {
                            session = Session_create(events[i].data.fd);
                            hashtable_ts_insert(&sock_to_session_hashtable, keyP, session);
                        }

                        buffer = session->stream.buffer;
                        session->stream.position = 0;

                        bytesRead = recv(events[i].data.fd, buffer, BUFSIZ, 0);
                        if (bytesRead == -1) {
                            /* If errno == EAGAIN, that means we have read all
                             data. So go back to the main loop. */
                            if (errno != EAGAIN)
                            {
                                perror ("read");
                                if(session->state != STATE_SENDING_GET)
                                    done = 1;
                            }
                            break;
                        } else if (bytesRead == 0) {
                            /* End of file. The remote has closed the
                               connection. */
                            if(session->state != STATE_SENDING_GET)
                                done = 1;
                            break;
                        }

                        session->stream.bytesInBuffer = bytesRead;

                        Session_processNext(session);

                        /* Write the buffer to standard output */
                        /*s = write (1, buffer, bytesRead);
                        if (s == -1) {
                            perror ("write");
                            exit(EXIT_FAILURE);
                        }*/
                        
                    }
                    
                    if(session && session->state == STATE_READING_PUT && done) {
                        if(session->totalWritten < session->totalBytesForPut) {
                            sending_put_response(session, "ERROR\nReceived too little data\n");
                            session->state = STATE_DONE;
                        }
                    }
                    
                    if (session && session->state != STATE_SENDING_GET && done)
                    {
                        LOG ("Closed connection on descriptor %d\n",
                                events[i].data.fd);

                        close (events[i].data.fd);

                        uint32_t keyP = events[i].data.fd;
                        hashtable_ts_free(&sock_to_session_hashtable, keyP);
                    } else if(session && (session->status == STATUS_SESSION_END || session->status == STATUS_SESSION_ERROR)) {
                        LOG ("Closed connection on descriptor %d\n",
                                events[i].data.fd);

                        close (events[i].data.fd);

                        uint32_t keyP = events[i].data.fd;
                        hashtable_ts_free(&sock_to_session_hashtable, keyP);
                    }
                        
                }
            }
        }
    }

    free (events);

    close (server_fd);

    return 0;
}


