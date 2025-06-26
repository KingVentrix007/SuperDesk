
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>

#pragma comment(lib, "Ws2_32.lib")

using json = nlohmann::json;
using namespace std;

#define SERVER_IP "192.168.0.26"
#define VIDEO_PORT 12345
#define INPUT_PORT 12346
#define RECONNECT_DELAY 2000 // in milliseconds
#define WINDOW_NAME "Remote Window"

SOCKET video_sock = INVALID_SOCKET;
SOCKET input_sock = INVALID_SOCKET;

atomic<bool> is_getting_vid_sock(false);
atomic<bool> is_getting_in_sock(false);
atomic<bool> window_open(false);
atomic<bool> can_make_window(true);

mutex click_mutex;
json click_position;

void mouseCallback(int event, int x, int y, int, void*) {
    lock_guard<mutex> lock(click_mutex);
    if (event == cv::EVENT_LBUTTONDOWN) {
        click_position = { {"type", "click"}, {"button", "left"}, {"x", x}, {"y", y} };
    } else if (event == cv::EVENT_RBUTTONDOWN) {
        click_position = { {"type", "click"}, {"button", "right"}, {"x", x}, {"y", y} };
    } else if (event == cv::EVENT_LBUTTONDBLCLK) {
        click_position = { {"type", "dclick"}, {"button", "left"}, {"x", x}, {"y", y} };
    }
}

SOCKET connectSocket(const char* ip, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

void connectVideo() {
    is_getting_vid_sock = true;
    while (video_sock == INVALID_SOCKET) {
        video_sock = connectSocket(SERVER_IP, VIDEO_PORT);
        if (video_sock != INVALID_SOCKET)
            cout << "[CLIENT] Connected to video stream\n";
        else
            this_thread::sleep_for(chrono::milliseconds(RECONNECT_DELAY));
    }
    is_getting_vid_sock = false;
}

void connectInput() {
    is_getting_in_sock = true;
    while (input_sock == INVALID_SOCKET) {
        input_sock = connectSocket(SERVER_IP, INPUT_PORT);
        if (input_sock != INVALID_SOCKET)
            cout << "[CLIENT] Connected to input control\n";
        else
            this_thread::sleep_for(chrono::milliseconds(RECONNECT_DELAY));
    }
    is_getting_in_sock = false;
}

bool recvAll(SOCKET sock, char* buffer, int total) {
    int received = 0;
    while (received < total) {
        int ret = recv(sock, buffer + received, total - received, 0);
        if (ret <= 0) return false;
        received += ret;
    }
    return true;
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    thread(connectVideo).detach();
    thread(connectInput).detach();

    while (true) {
        if (video_sock == INVALID_SOCKET || input_sock == INVALID_SOCKET) {
            this_thread::sleep_for(chrono::milliseconds(100));
            continue;
        }

        try {
            if (!window_open) {
                cv::namedWindow(WINDOW_NAME);
                cv::setMouseCallback(WINDOW_NAME, mouseCallback);
                window_open = true;
            }

            vector<char> buffer;
            while (true) {
                char size_buf[4];
                if (!recvAll(video_sock, size_buf, 4)) throw runtime_error("Video socket closed");

                int frame_size = ntohl(*(int*)size_buf);
                buffer.resize(frame_size);
                if (!recvAll(video_sock, buffer.data(), frame_size))
                    throw runtime_error("Video socket closed");

                cv::Mat rawData(1, frame_size, CV_8UC1, buffer.data());
                cv::Mat frame = cv::imdecode(rawData, cv::IMREAD_COLOR);
                if (!frame.empty()) {
                    cv::imshow(WINDOW_NAME, frame);
                }

                {
                    lock_guard<mutex> lock(click_mutex);
                    if (!click_position.is_null()) {
                        string msg = click_position.dump();
                        uint32_t len = htonl((uint32_t)msg.size());
                        send(input_sock, (char*)&len, 4, 0);
                        send(input_sock, msg.c_str(), (int)msg.size(), 0);
                        click_position = nullptr;
                    }
                }

                int key = cv::waitKey(1);
                if (key == 'q') throw runtime_error("Quit key");
                else if (key != -1 && key != 255) {
                    json key_event = { {"type", "key"}, {"key", string(1, (char)key)} };
                    string msg = key_event.dump();
                    uint32_t len = htonl((uint32_t)msg.size());
                    send(input_sock, (char*)&len, 4, 0);
                    send(input_sock, msg.c_str(), (int)msg.size(), 0);
                }
            }

        } catch (exception& e) {
            cerr << "[CLIENT] Disconnected or error: " << e.what() << endl;
            if (window_open) {
                cv::destroyAllWindows();
                window_open = false;
            }
            closesocket(video_sock);
            closesocket(input_sock);
            video_sock = INVALID_SOCKET;
            input_sock = INVALID_SOCKET;

            if (!is_getting_vid_sock) thread(connectVideo).detach();
            if (!is_getting_in_sock) thread(connectInput).detach();
            this_thread::sleep_for(chrono::milliseconds(RECONNECT_DELAY));
        }
    }

    WSACleanup();
    return 0;
}
