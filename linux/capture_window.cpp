#include <opencv2/opencv.hpp>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <thread>
#include <nlohmann/json.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>
#include <X11/Xatom.h>
#define PORT 12345
bool is_running = true;
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


void setWindowOpacity(Display* dpy, Window win, unsigned long opacity) {
    Atom property = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    if (property == None) {
        std::cerr << "No _NET_WM_WINDOW_OPACITY atom available\n";
        return;
    }
    XChangeProperty(dpy, win, property, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&opacity, 1);
    XFlush(dpy);
}
int wait_for_focus(Display* dpy, Window win, int timeout_ms = 5000) {
    Window focused_win;
    int elapsed = 0;
    const int interval = 10; // ms

    while (elapsed < timeout_ms) {
        int revert;
        XGetInputFocus(dpy, &focused_win, &revert);
        if (focused_win == win) {
            return 1; // success
        }
        usleep(interval * 1000);
        elapsed += interval;
    }
    return 0; // failed to get focus
}


void handle_input_events(Display* dpy, Window window, int port) {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(sock_fd, 1);
    std::cout << "[INPUT] Listening on port " << port << "...\n";
    int client_fd = accept(sock_fd, nullptr, nullptr);
    std::cout << "[INPUT] Client connected.\n";

    char header[4];
    std::vector<char> buffer;

    XWindowAttributes attr;
    XGetWindowAttributes(dpy, window, &attr);
    Window root = DefaultRootWindow(dpy);

    while (true) {
        int received = recv(client_fd, header, 4, MSG_WAITALL);
        if (received != 4) break;

        uint32_t msg_size;
        memcpy(&msg_size, header, 4);
        msg_size = ntohl(msg_size);

        buffer.resize(msg_size);
        received = recv(client_fd, buffer.data(), msg_size, MSG_WAITALL);
        if (received != msg_size) break;

        std::string json_str(buffer.begin(), buffer.end());
        try {
            auto msg = nlohmann::json::parse(json_str);
            // setWindowOpacity(dpy, window, 0x000000000);
            XRaiseWindow(dpy, window);
            XSetInputFocus(dpy, window, RevertToParent, CurrentTime);
            XFlush(dpy);
            XSync(dpy, False);
            XFlush(dpy);
            usleep(75000);
            if (!wait_for_focus(dpy, window, 5000)) {
            std::cerr << "[INPUT] Warning: window did not gain input focus after 5000ms\n";
            }
            if (msg["type"] == "click") {
                XWindowAttributes attr_check;
                if (!XGetWindowAttributes(dpy, window, &attr_check) || attr_check.map_state != IsViewable) {
                    std::cerr << "[INPUT] Target window not viewable or mapped\n";
                    continue;
                }

                int x = msg["x"];
                int y = msg["y"];
                std::string btn = msg["button"];
                std::cout << "x:" << attr.x <<"|y:"<<attr.y <<"\n";
                // Translate local coords to screen coords
                XWindowAttributes attr;
                XGetWindowAttributes(dpy, window, &attr);
                Window root = DefaultRootWindow(dpy);

                int abs_x, abs_y;
                Window dummy;
                XTranslateCoordinates(dpy, window, root, 0, 0, &abs_x, &abs_y, &dummy);

                int win_x = abs_x + x;
                int win_y = abs_y + y;

                // Move mouse
                XWarpPointer(dpy, None, root, 0, 0, 0, 0, win_x, win_y);
                XFlush(dpy);
                XSync(dpy, False);
                usleep(75000);
                // Simulate click
                int button = (btn == "right") ? 3 : 1;
                XTestFakeButtonEvent(dpy, button, True, CurrentTime);
                XTestFakeButtonEvent(dpy, button, False, CurrentTime);
                XFlush(dpy);
                XSync(dpy, False);
                std::cout << "[INPUT] Click " << btn << " at (" << x << "," << y << ")\n";
                // XLowerWindow(dpy, window);
                XSync(dpy, False);
                XFlush(dpy);
                usleep(75000);
                setWindowOpacity(dpy, window, 0xFFFFFFFF);
                XFlush(dpy);
            }
            else if (msg["type"] == "dclick") {
                int x = msg["x"];
                int y = msg["y"];
                std::string btn = msg["button"];

                // Translate local coords to screen coords
                XWindowAttributes attr;
                XGetWindowAttributes(dpy, window, &attr);
                Window root = DefaultRootWindow(dpy);

                int abs_x, abs_y;
                Window dummy;
                XTranslateCoordinates(dpy, window, root, 0, 0, &abs_x, &abs_y, &dummy);

                int win_x = abs_x + x;
                int win_y = abs_y + y;

                // Move mouse
                XWarpPointer(dpy, None, root, 0, 0, 0, 0, win_x, win_y);
                XFlush(dpy);

                // Simulate click
                int button = (btn == "right") ? 3 : 1;
                XTestFakeButtonEvent(dpy, button, True, CurrentTime);
                XTestFakeButtonEvent(dpy, button, False, CurrentTime);
                XFlush(dpy);
                usleep(150000);
                XTestFakeButtonEvent(dpy, button, True, CurrentTime);
                XTestFakeButtonEvent(dpy, button, False, CurrentTime);
                XFlush(dpy);
                std::cout << "[INPUT] Click " << btn << " at (" << x << "," << y << ")\n";
                // XLowerWindow(dpy, window);
                XSync(dpy, False);
                XFlush(dpy);
                usleep(75000);
                setWindowOpacity(dpy, window, 0xFFFFFFFF);
                XFlush(dpy);
            }
            else if (msg["type"] == "key") {
                    std::string key_str = msg["key"];
                    
                    // You need to map the key string to a keycode
                    if (key_str.size() == 1 && key_str[0] == 27)
                    {
                        std::cout << "[INPUT] Escape pressed, stopping output.\n";
                        is_running = false;
                        return;
                    }
                    KeySym keysym = XStringToKeysym(key_str.c_str());
                    
                    if (keysym == NoSymbol) {
                        std::cerr << "[INPUT] Unknown key: " << key_str << "\n";
                        return;
                    }
                    if (keysym == XK_Escape)
                    {
                        std::cout << "[INPUT] Escape pressed, stopping output.\n";
                    }
                    KeyCode keycode = XKeysymToKeycode(dpy, keysym);

                    // Focus window before sending key
                    XRaiseWindow(dpy, window);
                    XSetInputFocus(dpy, window, RevertToParent, CurrentTime);
                    XFlush(dpy);
                    usleep(100000);

                    // Send key press and release
                    XTestFakeKeyEvent(dpy, keycode, True, CurrentTime);  // key down
                    XTestFakeKeyEvent(dpy, keycode, False, CurrentTime); // key up
                    XFlush(dpy);

                    std::cout << "[INPUT] Key press: " << key_str << "\n";

                    // XLowerWindow(dpy, window);
                    XSync(dpy, False);
                    XFlush(dpy);
                    usleep(75000);
                    setWindowOpacity(dpy, window, 0xFFFFFFFF);
                    XFlush(dpy);
                }
            ;
            
        } catch (...) {
            std::cerr << "[INPUT] JSON parse error\n";
        }
    }

    close(client_fd);
    close(sock_fd);
}
void stream_window(Display* dpy, Window target_win, int client_socket) {
    while (is_running) {
        Pixmap pixmap = XCompositeNameWindowPixmap(dpy, target_win);
        XWindowAttributes attr{};
        XGetWindowAttributes(dpy, target_win, &attr);
        // XLowerWindow(dpy, target_win);
        XSync(dpy, False);
        XFlush(dpy);
        usleep(75000);
        setWindowOpacity(dpy, target_win, 0xFFFFFFFF);
        XFlush(dpy);
        XImage* image = XGetImage(dpy, pixmap, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
        if (!image) {
            std::cerr << "Failed to get XImage\n";
            break;
        }

        cv::Mat frame = ximageToMat(image);

        std::vector<uchar> buf;
        cv::imencode(".jpg", frame, buf, {cv::IMWRITE_JPEG_QUALITY, 80});

        uint32_t frame_size = htonl(buf.size());
        if (send(client_socket, &frame_size, sizeof(frame_size), 0) < 0) {
            perror("send frame size");
            break;
        }

        if (send(client_socket, buf.data(), buf.size(), 0) < 0) {
            perror("send frame data");
            break;
        }

        XDestroyImage(image);
        XFreePixmap(dpy, pixmap);

        usleep(33 * 1000);
    }
}
bool is_window_offscreen(Display* dpy, Window win) {
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, win, &attr);

    int x, y;
    Window child;
    XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), 0, 0, &x, &y, &child);

    int screen_width = DisplayWidth(dpy, DefaultScreen(dpy));
    int screen_height = DisplayHeight(dpy, DefaultScreen(dpy));

    // If part of window is outside screen bounds
    return (x + attr.width < 0 || x > screen_width ||
            y + attr.height < 0 || y > screen_height);
}
Window find_offscreen_window(Display* dpy) {
    Window root = DefaultRootWindow(dpy);
    Window root_return, parent_return;
    Window* children;
    unsigned int nchildren;

    if (XQueryTree(dpy, root, &root_return, &parent_return, &children, &nchildren) == 0)
        return 0;

    for (unsigned int i = 0; i < nchildren; ++i) {
        Window w = children[i];

        XWindowAttributes attr;
        if (!XGetWindowAttributes(dpy, w, &attr) || attr.map_state != IsViewable)
            continue;

        int x, y;
        Window dummy;
        XTranslateCoordinates(dpy, w, root, 0, 0, &x, &y, &dummy);

        int screen_width = DisplayWidth(dpy, DefaultScreen(dpy));
        int screen_height = DisplayHeight(dpy, DefaultScreen(dpy));

        if (x + attr.width < 0 || x > screen_width ||
            y + attr.height < 0 || y > screen_height) {
            return w;
        }
    }

    if (children)
        XFree(children);
    return 0; // No window offscreen
}

int main(int argc, char** argv) {
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

    int screen_width = DisplayWidth(dpy, DefaultScreen(dpy));
    int screen_height = DisplayHeight(dpy, DefaultScreen(dpy));

    // Setup server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

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

    std::cout << "Watching for windows dragged to screen edge...\n";

    int hold_counter = 0;
    const int hold_threshold = 20; // 20 * 100ms = 2 seconds

    while (true) {
        Window root = DefaultRootWindow(dpy);
        Window ret_root, ret_child;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;

        if (!XQueryPointer(dpy, root, &ret_root, &ret_child,
                           &root_x, &root_y, &win_x, &win_y, &mask)) {
            usleep(100000);
            continue;
        }

        bool is_pressed = mask & Button1Mask;
        bool at_edge = (root_x <= 1 || root_y <= 1 ||
                        root_x >= screen_width - 1 || root_y >= screen_height - 1);

        if (is_pressed && at_edge) {
            hold_counter++;
        } else {
            hold_counter = 0;
        }

        if (hold_counter >= hold_threshold) {
            std::cout << "[INFO] Edge drag detected. Trying to stream window...\n";
            hold_counter = 0;

            // Get active window
            Atom actual_type;
            int actual_format;
            unsigned long nitems, bytes_after;
            unsigned char* prop = nullptr;
            Atom active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
            Window active_win = 0;

            if (XGetWindowProperty(dpy, root, active, 0, (~0L), False, AnyPropertyType,
                                   &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
                if (nitems > 0) {
                    active_win = *(Window*)prop;
                }
                XFree(prop);
            }

            if (!active_win) {
                std::cerr << "[WARN] No active window found.\n";
                continue;
            }

            std::cout << "[INFO] Active window ID: " << active_win << "\n";

            // Redirect for composite capture
            XCompositeRedirectWindow(dpy, active_win, CompositeRedirectAutomatic);
            XFlush(dpy);

            // Start input thread
            std::thread input_thread(handle_input_events, dpy, active_win, 12346);
            input_thread.detach();

            std::cout << "[INFO] Waiting for client to connect on port " << PORT << "...\n";
            socklen_t addrlen = sizeof(address);
            int client_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
            if (client_socket < 0) {
                perror("accept");
                continue;
            }

            std::cout << "[INFO] Client connected, starting stream...\n";
            stream_window(dpy, active_win, client_socket);
            close(client_socket);

            std::cout << "[INFO] Stream ended. Watching for next edge drag...\n";
        }

        usleep(100000); // 100 ms delay
    }

    close(server_fd);
    XCloseDisplay(dpy);
    return 0;
}
