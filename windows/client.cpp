#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <thread>
#include <mutex>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <opencv2/opencv.hpp>
#include <nlohmann/json.hpp>

#pragma comment(lib, "Ws2_32.lib")

using json = nlohmann::json;

#define SERVER_IP "192.168.0.26"
#define VIDEO_PORT 12345
#define INPUT_PORT 12346
#define RECONNECT_DELAY 2000 // ms

SOCKET video_sock = INVALID_SOCKET;
SOCKET input_sock = INVALID_SOCKET;
std::atomic<bool> is_getting_vid_sock(false);
std::atomic<bool> is_getting_in_sock(false);
std::mutex click_mutex;
json click_position;

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

bool recvAll(SOCKET sock, char* buffer, int total) {
    int received = 0;
    while (received < total) {
        int ret = recv(sock, buffer + received, total - received, 0);
        if (ret <= 0) return false;
        received += ret;
    }
    return true;
}

class RemoteWindow : public QMainWindow {
    Q_OBJECT
public:
    QLabel* label;
    QTimer* timer;

    RemoteWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("Remote Window");
        resize(800, 600);
        label = new QLabel(this);
        label->setAlignment(Qt::AlignCenter);
        setCentralWidget(label);

        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &RemoteWindow::updateFrame);
        timer->start(10);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        std::lock_guard<std::mutex> lock(click_mutex);
        QString button = event->button() == Qt::LeftButton ? "left" : "right";
        click_position = { {"type", "click"}, {"button", button.toStdString()}, {"x", event->x()}, {"y", event->y()} };
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (input_sock == INVALID_SOCKET) return;
        char c = event->text().toStdString()[0];
        json key_event = { {"type", "key"}, {"key", std::string(1, c)} };
        std::string msg = key_event.dump();
        uint32_t len = htonl((uint32_t)msg.size());
        send(input_sock, (char*)&len, 4, 0);
        send(input_sock, msg.c_str(), (int)msg.size(), 0);
    }

private slots:
    void updateFrame() {
        if (video_sock == INVALID_SOCKET) return;

        try {
            char size_buf[4];
            if (!recvAll(video_sock, size_buf, 4)) throw std::runtime_error("Video socket closed");

            int frame_size = ntohl(*(int*)size_buf);
            std::vector<char> buffer(frame_size);
            if (!recvAll(video_sock, buffer.data(), frame_size)) throw std::runtime_error("Video socket closed");

            cv::Mat rawData(1, frame_size, CV_8UC1, buffer.data());
            cv::Mat frame = cv::imdecode(rawData, cv::IMREAD_COLOR);
            if (!frame.empty()) {
                cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
                QImage img(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
                label->setPixmap(QPixmap::fromImage(img).scaled(label->size(), Qt::KeepAspectRatio));
            }

            std::lock_guard<std::mutex> lock(click_mutex);
            if (!click_position.is_null()) {
                std::string msg = click_position.dump();
                uint32_t len = htonl((uint32_t)msg.size());
                send(input_sock, (char*)&len, 4, 0);
                send(input_sock, msg.c_str(), (int)msg.size(), 0);
                click_position = nullptr;
            }

        } catch (std::exception& e) {
            qWarning("[CLIENT] Disconnected or error: %s", e.what());
            closesocket(video_sock);
            closesocket(input_sock);
            video_sock = INVALID_SOCKET;
            input_sock = INVALID_SOCKET;
            if (!is_getting_vid_sock) std::thread(connectVideo).detach();
            if (!is_getting_in_sock) std::thread(connectInput).detach();
        }
    }
};

void connectVideo() {
    is_getting_vid_sock = true;
    while (video_sock == INVALID_SOCKET) {
        video_sock = connectSocket(SERVER_IP, VIDEO_PORT);
        if (video_sock != INVALID_SOCKET)
            std::cout << "[CLIENT] Connected to video stream\n";
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY));
    }
    is_getting_vid_sock = false;
}

void connectInput() {
    is_getting_in_sock = true;
    while (input_sock == INVALID_SOCKET) {
        input_sock = connectSocket(SERVER_IP, INPUT_PORT);
        if (input_sock != INVALID_SOCKET)
            std::cout << "[CLIENT] Connected to input control\n";
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY));
    }
    is_getting_in_sock = false;
}

int main(int argc, char *argv[]) {
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::thread(connectVideo).detach();
    std::thread(connectInput).detach();

    QApplication app(argc, argv);
    RemoteWindow window;
    window.show();
    int ret = app.exec();

    closesocket(video_sock);
    closesocket(input_sock);
    WSACleanup();
    return ret;
}

#include "main.moc"
