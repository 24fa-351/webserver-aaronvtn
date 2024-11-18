#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>

#define DEFAULT_PORT 80
#define BUFFER_SIZE 1024
#define STATIC_DIR "./static"

int totalRequests = 0;
int totalReceivedBytes = 0;
int totalSentBytes = 0;
pthread_mutex_t statsMutex = PTHREAD_MUTEX_INITIALIZER;

void incrementStats(int receivedBytes, int sentBytes) {
    pthread_mutex_lock(&statsMutex);
    totalRequests++;
    totalReceivedBytes += receivedBytes;
    totalSentBytes += sentBytes;
    pthread_mutex_unlock(&statsMutex);
}

void sendResponse(int clientFd, const char *statusCode, const char *contentType, const char *body, size_t bodySize) {
    char header[BUFFER_SIZE];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n", statusCode, contentType, bodySize);
    send(clientFd, header, strlen(header), 0);
    send(clientFd, body, bodySize, 0);
    incrementStats(strlen(header) + bodySize, bodySize);
}

void handleStatic(int clientFd, const char *path) {
    char filePath[BUFFER_SIZE];
    snprintf(filePath, sizeof(filePath), "%s%s", STATIC_DIR, path);
    
    int fileFd = open(filePath, O_RDONLY);
    if (fileFd == -1) {
        sendResponse(clientFd, "404 Not Found", "text/plain", "File Not Found", strlen("File Not Found"));
        return;
    }
    
    // Parse file extension
    const char *contentType = "application/octet-stream"; // Default to binary
    if (strstr(path, ".png")) {
        contentType = "image/png";
    } else if (strstr(path, ".jpg") || strstr(path, ".jpeg")) {
        contentType = "image/jpeg";
    } else if (strstr(path, ".gif")) {
        contentType = "image/gif";
    } else if (strstr(path, ".bmp")) {
        contentType = "image/bmp";
    } else if (strstr(path, ".webp")) {
        contentType = "image/webp";
    }

    struct stat fileStat;
    fstat(fileFd, &fileStat);
    char *fileContent = malloc(fileStat.st_size);
    read(fileFd, fileContent, fileStat.st_size);
    close(fileFd);

    sendResponse(clientFd, "200 OK", contentType, fileContent, fileStat.st_size);
    free(fileContent);
}

void handleStats(int clientFd) {
    char stats[BUFFER_SIZE];
    snprintf(stats, sizeof(stats),
             "<html><body><h1>Server Stats</h1>"
             "<p>Total Requests: %d</p>"
             "<p>Total Bytes Received: %d</p>"
             "<p>Total Bytes Sent: %d</p></body></html>", 
             totalRequests, totalReceivedBytes, totalSentBytes);
    
    sendResponse(clientFd, "200 OK", "text/html", stats, strlen(stats));
}

void handleCalc(int clientFd, const char *query) {
    int a = 0, b = 0;
    if (sscanf(query, "a=%d&b=%d", &a, &b) == 2) {
        int result = a + b;
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), 
                 "<html><body><h1>Calculation Result</h1>"
                 "<p>%d + %d = %d</p></body></html>", a, b, result);
        sendResponse(clientFd, "200 OK", "text/html", response, strlen(response));
    } else {
        sendResponse(clientFd, "400 Bad Request", "text/plain", "Invalid Parameters", strlen("Invalid Parameters"));
    }
}

void *handleRequest(void *arg) {
    int clientFd = *((int *)arg);
    free(arg);
    
    char buffer[BUFFER_SIZE];
    ssize_t received = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        close(clientFd);
        return NULL;
    }
    
    buffer[received] = '\0';
    incrementStats(received, 0);

    char method[16], path[256], version[16];
    sscanf(buffer, "%s %s %s", method, path, version);
    
    if (strncmp(path, "/static", 7) == 0) {
        handleStatic(clientFd, path + 7);
    } else if (strcmp(path, "/stats") == 0) {
        handleStats(clientFd);
    } else if (strncmp(path, "/calc", 5) == 0) {
        const char *query = strchr(path, '?') + 1;
        handleCalc(clientFd, query);
    } else {
        sendResponse(clientFd, "404 Not Found", "text/plain", "Not Found", strlen("Not Found"));
    }
    
    close(clientFd);
    return NULL;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i + 1]);
            i++;
        }
    }
    
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in serverAddr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)
    };

    if (bind(serverFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Binding failed");
        close(serverFd);
        exit(EXIT_FAILURE);
    }

    if (listen(serverFd, 10) == -1) {
        perror("Listen failed");
        close(serverFd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);

    while (1) {
        int *clientFd = malloc(sizeof(int));
        *clientFd = accept(serverFd, NULL, NULL);
        if (*clientFd == -1) {
            perror("Accept failed");
            free(clientFd);
            continue;
        }

        pthread_t thread;
        if (pthread_create(&thread, NULL, handleRequest, clientFd) != 0) {
            perror("Thread creation failed");
            free(clientFd);
        } else {
            pthread_detach(thread);
        }
    }

    close(serverFd);
    return 0;
}
