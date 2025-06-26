#include <opencv2/opencv.hpp>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xutil.h>

#include <iostream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>

#define PORT 12345

// Recursive function to find window by title substring
Window findWindow(Display* dpy, Window root, const char* title_substr) {
    Window ret = 0;
    Window root_return, parent_return;
    Window* children;
    unsigned int nchildren;

    char* window_name = nullptr;
    if (XFetchName(dpy, root, &window_name)) {
        if (window_name) {
            if (strstr(window_name, title_substr)) {
                ret = root;
                XFree(window_name);
                return ret;
            }
            XFree(window_name);
        }
    }

    if (XQueryTree(dpy, root, &root_return, &parent_return, &children, &nchildren) == 0)
        return 0;

    for (unsigned int i = 0; i < nchildren; i++) {
        ret = findWindow(dpy, children[i], title_substr);
        if (ret)
            break;
    }

    if (children)
        XFree(children);
    return ret;
}

// Convert XImage to OpenCV Mat (BGRA)
cv::Mat ximageToMat(XImage* image) {
    int width = image->width;
    int height = image->height;
    cv::Mat mat(height, width, CV_8UC4);
    memcpy(mat.data, image->data, width * height * 4);
    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: ./capture_stream <window_title_substring>\n";
        return 1;
    }

    const char* target_title = argv[1];
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "Cannot open display\n";
        return 1;
    }

    int composite_event_base, composite_error_base;
    if (!XCompositeQueryExtension(dpy, &composite_event_base, &composite_error_base)) {
        std::cerr << "XComposite extension not available\n";
        XCloseDisplay(dpy);
        return 1;
    }

    Window root = DefaultRootWindow(dpy);
    Window target_win = findWindow(dpy, root, target_title);
    if (!target_win) {
        std::cerr << "Window with title containing '" << target_title << "' not found\n";
        XCloseDisplay(dpy);
        return 1;
    }

    std::cout << "Found window: " << target_win << "\n";

    // Redirect window for off-screen capture
    XCompositeRedirectWindow(dpy, target_win, CompositeRedirectAutomatic);
    XFlush(dpy);

    // Setup TCP server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        return 1;
    }

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return 1;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Waiting for client connection on port " << PORT << "...\n";

    socklen_t addrlen = sizeof(address);
    int client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
    if (client_socket < 0) {
        perror("accept");
        return 1;
    }
    std::cout << "Client connected!\n";

    while (true) {
        Pixmap pixmap = XCompositeNameWindowPixmap(dpy, target_win);
        XWindowAttributes attr{};
        XGetWindowAttributes(dpy, target_win, &attr);

        XImage* image = XGetImage(dpy, pixmap, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
        if (!image) {
            std::cerr << "Failed to get XImage\n";
            break;
        }

        cv::Mat frame = ximageToMat(image);

        // Encode frame as JPEG
        std::vector<uchar> buf;
        cv::imencode(".jpg", frame, buf, {cv::IMWRITE_JPEG_QUALITY, 80});

        // Send frame size (4 bytes network byte order)
        uint32_t frame_size = htonl(buf.size());
        if (send(client_socket, &frame_size, sizeof(frame_size), 0) < 0) {
            perror("send frame size");
            break;
        }

        // Send frame data
        if (send(client_socket, buf.data(), buf.size(), 0) < 0) {
            perror("send frame data");
            break;
        }

        XDestroyImage(image);
        XFreePixmap(dpy, pixmap);

        // Limit frame rate to ~30 FPS
        usleep(33 * 1000);
    }

    close(client_socket);
    close(server_fd);
    XCloseDisplay(dpy);

    return 0;
}
