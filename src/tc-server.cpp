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

const int MAX_CONN = 1024;

atomic<bool> stop (false);

int _socket;
vector<int> clients;
int bufsize = 1024;
int port = 5555;

pthread_mutex_t message_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t flush_messages = PTHREAD_COND_INITIALIZER;
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
    pthread_cond_signal(&flush_messages);
    for (int i = 0; i < clients.size(); i++) {
        shutdown(clients[i], SHUT_RDWR);
        close(clients[i]);
    }
    shutdown(_socket, SHUT_RDWR);
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

    listen(_socket, MAX_CONN);

    cout << "Waiting for clients to connect..." << endl;

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

void *serverRecv(void *client) {

    int _client = *((int *)client);

    while (!stop) {

        vector<string> messages;
        char buffer[bufsize];
        ssize_t n = recv(_client, buffer, bufsize, 0);

        if (n <= 0) {
            break;
        }
        bool finished = appendBuffer(messages, buffer, n, true);
        while (!finished && n > 0 && !stop) {
            n = recv(_client, buffer, bufsize, 0);
            if (n <= 0) {
                break;
            }
            finished = appendBuffer(messages, buffer, n, false);
        }
        if (n <= 0) {
            break;
        }
        if (messages.size() > 0) {
            pthread_mutex_lock(&message_mutex);
            for (int i = 0; i < messages.size(); i++) {
                message_queue.push_back(messages[i]);
            }
            pthread_mutex_unlock(&message_mutex);
            pthread_cond_signal(&flush_messages);
        }
    }

    pthread_exit(NULL);

}

void *listenNewConnections(void*) {

    vector<pthread_t> recv_threads;

    while (!stop) {

        struct sockaddr_in client_addr;
        socklen_t size = sizeof(client_addr);

        int client = accept(_socket, (struct sockaddr *)&client_addr, &size);

        if (client < 0 && stop) {
            break;
        }

        if (client < 0) {
            cout << "Error: can't accept connections" << endl;
            exit(1);
        }

        pthread_mutex_lock(&client_mutex);
        clients.push_back(client);
        pthread_mutex_unlock(&client_mutex);

        cout << "Connected client IP address: " << inet_ntoa(client_addr.sin_addr) << endl;

        pthread_t recv_thread;
        int r = pthread_create(&recv_thread, NULL, &serverRecv, &client);
        if (r) {
            cout << endl << "Error: failed to connect a client" << endl;
            exit(1);
        }

        recv_threads.push_back(recv_thread);
    }

    for (int i = 0; i < recv_threads.size(); i++) {
        pthread_join(recv_threads[i], NULL);
    }

    pthread_exit(NULL);
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

    vector<int> closed_clients;
    for (int i = 0; i < clients.size(); i++) {
        ssize_t n = send(clients[i], buf, strlen(buf), MSG_CONFIRM | MSG_NOSIGNAL);
        if (n < 0) {
            closed_clients.push_back(i);
        }
    }
    for (int i = 0; i < closed_clients.size(); i++) {
        close(clients[closed_clients[i]]);
        clients.erase(clients.begin() + closed_clients[i]);
    }
}

void *serverSend(void*) {
    int n;
    while (!stop) {
        vector<string> messages;
        pthread_cond_wait(&flush_messages, &message_mutex);
        n = message_queue.size();
        for (int i = 0; i < n; i++) {
            messages.push_back(message_queue[i]);
        }
        message_queue.clear();
        pthread_mutex_unlock(&message_mutex);
        for (int i = 0; i < n; i++) {
            sendMessage(messages[i].c_str());
        }
    }
    pthread_exit(NULL);
}

int main() {

    startServer();

    pthread_t listen_thread, server_send_thread, keypress_thread;
    int r1 = pthread_create(&listen_thread, NULL, &listenNewConnections, NULL);
    int r2 = pthread_create(&server_send_thread, NULL, &serverSend, NULL);
    if (r1 || r2) {
        cout << endl << "Error: failed to start" << endl;
        exit(1);
    }
    pthread_create(&keypress_thread, NULL, &readKeypress, NULL);

    pthread_join(listen_thread, NULL);
    pthread_join(server_send_thread, NULL);
    pthread_join(keypress_thread, NULL);

    cout << "stopped" << endl;

    exit(0);

}
