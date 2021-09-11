#include <iostream>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <atomic>
#include <vector>
#include <string>


using namespace std;

atomic<bool> stop (false);

int _socket, server;
int bufsize = 1024;
int port = 5555;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t have_new_messages = PTHREAD_COND_INITIALIZER;
vector<string> message_queue = {};


void *readKeypress(void*) {
    while (true) {
        cout << "Enter [q] to quit" << endl;
        char k;
        cin >> k;
        if (k == 'q') {
            break;
        }
    }
    cout << "stopping" << endl;
    stop = true;
    shutdown(server, SHUT_RD);
    close(server);
    close(_socket);
    pthread_exit(NULL);
}

void startServer() {
    _socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_socket < 0) {
        cout << "Error: can't open a socket" << endl;
        exit(1);
    }
    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if ((bind(_socket, (struct sockaddr*)&server_addr, sizeof(server_addr))) < 0) {
        cout << "Error: error binding connection, is port already being used?" << endl;
        exit(1);
    }

    cout << "Waiting for clients to connect..." << endl;

    listen(_socket, 1);

    struct sockaddr_in client_addr;
    socklen_t size = sizeof(client_addr);

    server = accept(_socket, (struct sockaddr *)&client_addr, &size);

    if (server < 0) {
        cout << "Error: can't accept connections" << endl;
        exit(1);
    }

    cout << "Connected client IP address: " << inet_ntoa(client_addr.sin_addr) << endl;
}

void sendMessage(char const* msg) {
    if (stop) {
        return;
    }
    char* buf = new char[strlen(msg) + 2];
    buf[0] = '\x02';
    for (int i = 0; i < strlen(msg) + 1; i++) {
        buf[i+1] = msg[i];
    }
    buf[strlen(msg)+1] = '\x03';
    send(server, buf, strlen(buf), 0);
}

bool appendBuffer(vector<string>& msgs, char *buf, ssize_t n, bool start) {
    bool finished = start;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\x02') {
            finished = false;
            msgs.push_back("");
        } else if (buf[i] == '\x03') {
            finished = true;
        } else if (!finished && msgs.size() > 0) {
            msgs[msgs.size()-1] += buf[i];
        }
    }
    return finished;
}

void *serverRecv(void*) {
    int e = 0;
    while (!stop) {
        vector<string> messages;
        char buffer[bufsize];
        ssize_t n = recv(server, buffer, bufsize, 0);
        if (n == 0) {
            continue;
        }
        if (n < 1) {
            e += 1;
            continue;
        }
        bool finished = appendBuffer(messages, buffer, n, true);
        while (!finished && n > 0 && !stop) {
            n = recv(server, buffer, bufsize, 0);
            finished = appendBuffer(messages, buffer, n, false);
        }

        if (messages.size() > 0) {

            pthread_mutex_lock(&mutex);
            for (int i = 0; i < messages.size(); i++) {
                message_queue.push_back(messages[i]);
            }
            pthread_mutex_unlock(&mutex);
            pthread_cond_signal(&have_new_messages);

        }

    }
    cout << "n errors: " << e << endl;
    pthread_cond_signal(&have_new_messages);
    pthread_exit(NULL);
}

void *serverSend(void*) {
    int n;
    while (!stop) {
        vector<string> messages;
        pthread_cond_wait(&have_new_messages, &mutex);
        n = message_queue.size();
        for (int i = 0; i < n; i++) {
            messages.push_back(message_queue[i]);
        }
        message_queue.clear();
        pthread_mutex_unlock(&mutex);
        for (int i = 0; i < n; i++) {
            sendMessage(messages[i].c_str());
        }
    }
    pthread_exit(NULL);
}


int main() {

    startServer();

    pthread_t server_recv_thread, server_send_thread, keypress_thread;
    int r1 = pthread_create(&server_recv_thread, NULL, &serverRecv, NULL);
    int r2 = pthread_create(&server_send_thread, NULL, &serverSend, NULL);
    if (r1 || r2) {
        cout << endl << "Error: failed to create a thread" << endl;
        exit(1);
    }
    int r3 = pthread_create(&keypress_thread, NULL, &readKeypress, NULL);

    pthread_join(server_recv_thread, NULL);
    pthread_join(server_send_thread, NULL);
    pthread_join(keypress_thread, NULL);

    exit(0);

}
