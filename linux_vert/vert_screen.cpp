// === Server (Linux Virtual Display Streamer) ===

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <opencv2/opencv.hpp>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <thread>

#define WIDTH  1280
#define HEIGHT 720
#define PORT   12345
#define CLIENT_IP "192.168.0.26"  // Change to the receiver's IP

cv::Mat capture_frame(Display* display, Window root) {
    XImage* img = XGetImage(display, root, 0, 0, WIDTH, HEIGHT, AllPlanes, ZPixmap);
    cv::Mat frame(HEIGHT, WIDTH, CV_8UC4, img->data);
    cv::Mat bgr;
    cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);
    cv::Mat cloned = bgr.clone();
    XDestroyImage(img);
    return cloned;
}

void stream_frame(const cv::Mat& frame) {
    std::vector<uchar> buffer;
    cv::imencode(".jpg", frame, buffer);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, CLIENT_IP, &addr.sin_addr);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) >= 0) {
        uint32_t size = htonl(buffer.size());
        send(sock, &size, sizeof(size), 0);
        send(sock, buffer.data(), buffer.size(), 0);
    }
    close(sock);
}

void run_server() {
    Display* display = XOpenDisplay(nullptr);
    Window root = DefaultRootWindow(display);

    while (true) {
        cv::Mat frame = capture_frame(display, root);
        stream_frame(frame);
        usleep(33000); // ~30 FPS
    }

    XCloseDisplay(display);
}

// === Client (Receiver) ===

void run_client() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 1);

    std::cout << "Waiting for connection...\n";
    int client_sock = accept(server_sock, nullptr, nullptr);
    std::cout << "Client connected.\n";

    while (true) {
        uint32_t size_net;
        if (recv(client_sock, &size_net, sizeof(size_net), MSG_WAITALL) <= 0) break;
        uint32_t size = ntohl(size_net);

        std::vector<uchar> buffer(size);
        int received = 0;
        while (received < size) {
            int r = recv(client_sock, buffer.data() + received, size - received, 0);
            if (r <= 0) break;
            received += r;
        }

        cv::Mat frame = cv::imdecode(buffer, cv::IMREAD_COLOR);
        if (!frame.empty()) {
            cv::imshow("Remote", frame);
            if (cv::waitKey(1) == 27) break;
        }
    }

    close(client_sock);
    close(server_sock);
}

int main() {
    std::thread server(run_server);
    std::thread client(run_client);
    server.join();
    client.join();
    return 0;
}