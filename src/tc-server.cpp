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
#include <chrono>


using namespace std;

const int MAX_CONN = 1024;

atomic<bool> stop (false);

int server_socket;
int bufsize = 1024;
int port = 5555;

pthread_mutex_t message_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t flush_messages = PTHREAD_COND_INITIALIZER;

struct client_struct {
    int socket;
    string user_name;
};
vector<client_struct> clients;

struct message_struct {
    string user_name;
    int64_t timestamp;
    string text;
};
vector<message_struct> message_history;


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
        shutdown(clients[i].socket, SHUT_RDWR);
        close(clients[i].socket);
    }
    shutdown(server_socket, SHUT_RDWR);
    close(server_socket);
    pthread_exit(NULL);
}

void startServer() {

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket < 0) {
        cout << "Error: can't open a socket" << endl;
        exit(1);
    }

    struct sockaddr_in server_addr;

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if ((bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr))) < 0) {
        cout << "Error: error binding connection, is port already being used?" << endl;
        exit(1);
    }

    listen(server_socket, MAX_CONN);

    cout << "Waiting for clients to connect..." << endl;

}

bool parseBuffer(vector<string>& messages, char *buffer, ssize_t n, bool start) {
    bool finished = start;
    for (int i = 0; i < n; i++) {
        if (buffer[i] == '\x02') {
            finished = false;
            messages.push_back("");
        } else if (buffer[i] == '\x03') {
            finished = true;
        } else if (!finished && messages.size() > 0) {
            messages[messages.size()-1] += buffer[i];
        }
    }
    return finished;
}

void *serverRecv(void *c) {

    client_struct client = *((client_struct *)c);

    while (!stop) {

        vector<string> messages;
        char buffer[bufsize];
        ssize_t n = recv(client.socket, buffer, bufsize, 0);
        if (n <= 0) {
            break;
        }
        bool finished = parseBuffer(messages, buffer, n, true);
        while (!finished && n > 0 && !stop) {
            n = recv(client.socket, buffer, bufsize, 0);
            if (n <= 0) {
                break;
            }
            finished = parseBuffer(messages, buffer, n, false);
        }
        if (n <= 0) {
            break;
        }
        if (messages.size() > 0) {
            int64_t ts = chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch()).count();
            pthread_mutex_lock(&message_mutex);
            for (int i = 0; i < messages.size(); i++) {
                message_struct m = {client.user_name, ts, messages[i]};
                message_history.push_back(m);
            }
            pthread_mutex_unlock(&message_mutex);
            pthread_cond_signal(&flush_messages);
        }
    }

    pthread_exit(NULL);

}

string getUserName(int client_socket) {
    string user_name;
    char buffer[bufsize];
    bool finished = false;
    while (!finished) {
        ssize_t n = recv(client_socket, buffer, bufsize, 0);
        for (int i = 0; i < n; i++) {
            if (buffer[i] == '\x02') {
                continue;
            } else if (buffer[i] == '\x03') {
                finished = true;
                break;
            } else if (!finished) {
                user_name += buffer[i];
            }
        }
    }
    return user_name;
}

void sendMessagesToOne(int start, client_struct client) {
    pthread_mutex_lock(&message_mutex);
    for (int i = start; i < message_history.size(); i++) {
        string msg_text = message_history[i].user_name + ": " + message_history[i].text;
        int buf_len = msg_text.length() + 2;
        char* buf = new char[buf_len];
        buf[0] = '\x02';
        for (int i = 0; i < buf_len - 1; i++) {
            buf[i+1] = msg_text[i];
        }
        buf[buf_len-1] = '\x03';
        ssize_t n = send(client.socket, buf, buf_len, MSG_CONFIRM | MSG_NOSIGNAL);
        if (n < 0) {
            break;
        }
    }
    pthread_mutex_unlock(&message_mutex);
}

void *listenNewConnections(void*) {

    vector<pthread_t> recv_threads;

    while (!stop) {

        struct sockaddr_in client_addr;
        socklen_t size = sizeof(client_addr);

        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &size);

        if (client_socket < 0 && stop) {
            break;
        }

        if (client_socket < 0) {
            cout << "Error: can't accept connections" << endl;
            exit(1);
        }

        string user_name = getUserName(client_socket) + "@" + inet_ntoa(client_addr.sin_addr);
        client_struct client = {client_socket, user_name};

        sendMessagesToOne(0, client);

        pthread_mutex_lock(&client_mutex);
        clients.push_back(client);
        pthread_mutex_unlock(&client_mutex);

        pthread_t recv_thread;
        int r = pthread_create(&recv_thread, NULL, &serverRecv, &clients[clients.size()-1]);
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

void sendMessageToAll(message_struct message) {

    if (stop) {
        return;
    }

    string msg_text = message.user_name + ": " + message.text;
    int buf_len = msg_text.length() + 2;
    char* buf = new char[buf_len];
    buf[0] = '\x02';
    for (int i = 0; i < buf_len - 1; i++) {
        buf[i+1] = msg_text[i];
    }
    buf[buf_len-1] = '\x03';

    vector<int> closed_clients;
    for (int i = 0; i < clients.size(); i++) {
        ssize_t n = send(clients[i].socket, buf, buf_len, MSG_CONFIRM | MSG_NOSIGNAL);
        if (n < 0) {
            closed_clients.push_back(i);
        }
    }
    if (closed_clients.size() > 0) {
        pthread_mutex_lock(&client_mutex);
        for (int i = 0; i < closed_clients.size(); i++) {
            close(clients[closed_clients[i]].socket);
            clients.erase(clients.begin() + closed_clients[i]);
        }
        pthread_mutex_unlock(&client_mutex);
    }
}

void *serverSend(void*) {
    int last_sent = 0;
    while (!stop) {
        vector<message_struct> messages;
        pthread_cond_wait(&flush_messages, &message_mutex);
        for (int i = last_sent; i < message_history.size(); i++) {
            messages.push_back(message_history[i]);
        }
        pthread_mutex_unlock(&message_mutex);
        for (int i = 0; i < messages.size(); i++) {
            sendMessageToAll(messages[i]);
        }
        last_sent += messages.size();
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
