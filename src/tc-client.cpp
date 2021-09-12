#include <locale.h>
#include <ncurses.h>
#include <cstdlib>
#include <pthread.h>
#include <atomic>
#include <vector>
#include <string>
#include <iostream>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>


using namespace std;

atomic<bool> stop (false);

int _socket;
int port = 5555;
int bufsize = 1024;
const char* ip = "0";

WINDOW* win_in;
WINDOW* win_out;


void connect() {

    _socket = socket(AF_INET, SOCK_STREAM, 0);

    if (_socket < 0) {
        cout << "Error: can't open socket" << endl;
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
        cout << "Connected to the server: " << ip << ", port number: " << port << endl;
    } else {
        cout << "Error: can't connect to the server" << endl;
        exit(1);
    }
}

void sendMessage(char* msg) {
    char* buf = new char[strlen(msg) + 2];
    buf[0] = '\x02';
    for (int i = 0; i < strlen(msg) + 1; i++) {
        buf[i+1] = msg[i];
    }
    buf[strlen(msg)+1] = '\x03';
    ssize_t n = send(_socket, buf, strlen(buf), MSG_CONFIRM | MSG_NOSIGNAL);
    if (n < 0) {
        stop = true;
        shutdown(_socket, SHUT_RDWR);
        close(_socket);
        endwin();
        cout << "disconnected" << endl;
    }
}

bool appendBuffer(vector<string>& msgs, char *buf, ssize_t n) {
    bool finished = false;
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

void *recieveMessages(void*) {
    while (!stop) {
        vector<string> messages;
        char buffer[bufsize];
        ssize_t n = recv(_socket, buffer, bufsize, 0);
        if (n <= 0) {
            break;
        }
        bool finished = appendBuffer(messages, buffer, n);
        while (!finished && n > 0) {
            n = recv(_socket, buffer, bufsize, 0);
            if (n <= 0) {
                break;
            }
            finished = appendBuffer(messages, buffer, n);
        }
        if (n <= 0) {
            break;
        }

        if (messages.size() > 0) {
            for (int i = 0; i < messages.size(); i++) {
                wprintw(win_out, "@: %s\n", messages[i].c_str());
            }
            wrefresh(win_out);
            wprintw(win_in, "");
            wrefresh(win_in);
            messages.clear();
        }
    }
    if (!stop) {
        stop = true;
        shutdown(_socket, SHUT_RDWR);
        close(_socket);
        endwin();
        cout << "disconnected" << endl;
    }
    pthread_exit(NULL);
}

void buildWindow() {
    setlocale(LC_ALL, "");

    initscr();

    int sh, sw;
    getmaxyx(stdscr, sh, sw);

    int wx = 0;
    int wy = 0;
    int ww = sw - 2*wx;
    int wh = sh - 2*wy;
    int input_lines = wh/4;

    WINDOW* _win = newwin(wh, ww, wy, wx);
    wborder(_win, 0, 0, 0, 0, 0, 0, 0, 0);
    win_out = derwin(_win, wh-input_lines-2, ww-2, 1, 1);

    WINDOW* _win_in = derwin(_win, input_lines, ww, wh-input_lines, 0);
    wborder(_win_in, 0, 0, 0, 0, ACS_LTEE, ACS_RTEE, 0, 0);
    win_in = derwin(_win_in, input_lines-2, ww-2, 1, 1);
    wtimeout(win_in, 1000);
    wmove(win_in, 0, 0);
    wprintw(win_in, ">");

    refresh();
    wrefresh(_win);
    wrefresh(win_out);
    wrefresh(_win_in);
    wrefresh(win_in);
}

void *getInput(void*) {
    noecho();
    keypad(win_in, TRUE);
    int ch;
    string input = "";
    while ((ch = wgetch(win_in)) != 27 && !stop) {
        if (ch == KEY_ENTER || ch == '\n') {
            if (input.length() > 0) {
                sendMessage((char*)input.c_str());
                input = "";
            }
            werase(win_in);
            wmove(win_in, 0, 0);
            wprintw(win_in, ">");
        } else if (ch == KEY_BACKSPACE || ch == 127) {
            input = input.substr(0, input.length()-1);
            werase(win_in);
            wmove(win_in, 0, 0);
            wprintw(win_in, ">");
            wprintw(win_in, input.c_str());
        } else if (ch >= 32 && ch <= 126) {
            input += ch;
            waddch(win_in, ch);
        } else if (ch == KEY_UP) {
            // @TODO: scroll up
        } else if (ch == KEY_DOWN) {
            // @TODO: scroll down
        }
    }
    if (!stop) {
        stop = true;
        shutdown(_socket, SHUT_RDWR);
        close(_socket);
        endwin();
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {

    if (argc > 1) {
        ip = argv[1];
    }

    connect();
    buildWindow();

    pthread_t recv_thread, input_thread;
    int r1 = pthread_create(&recv_thread, NULL, &recieveMessages, NULL);
    int r2 = pthread_create(&input_thread, NULL, &getInput, NULL);
    if (r1 || r2) {
        cout << endl << "Error: failed to create a threads" << endl;
        exit(1);
    }

    pthread_join(recv_thread, NULL);
    pthread_join(input_thread, NULL);

    return 0;
}
