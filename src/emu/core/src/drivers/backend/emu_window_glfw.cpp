#include <drivers/backend/emu_window_glfw.h>
#include <common/log.h>
#include <common/raw_bind.h>

#define CALL_IF_VALID(_a, ...) if (_a) {_a(##__VA_ARGS__);}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    eka2l1::driver::emu_window_glfw3 *win = reinterpret_cast<decltype(win)>(glfwGetWindowUserPointer(window));

    if (action == GLFW_PRESS) {
        CALL_IF_VALID(win->button_pressed, key);
    }
    else if (action == GLFW_RELEASE) {
        CALL_IF_VALID(win->button_released);
    }
    else {
        CALL_IF_VALID(win->button_hold, key);
    }
}

void mouse_callback(GLFWwindow* window, int button, int action, int mods) {
    eka2l1::driver::emu_window_glfw3 *win = reinterpret_cast<decltype(win)>(glfwGetWindowUserPointer(window));

    if (button != GLFW_MOUSE_BUTTON_LEFT) {
        return;
    }

    double x; double y;
    glfwGetCursorPos(window, &x, &y);

    if (action == GLFW_PRESS) {
        CALL_IF_VALID(win->touch_pressed, eka2l1::point(x, y));
    }
    else if (action == GLFW_REPEAT) {
        CALL_IF_VALID(win->touch_move, eka2l1::point(x, y));
    }
    else {
        CALL_IF_VALID(win->touch_released);
    }
}

void fb_resize_callback(GLFWwindow* window, int width, int height) {
    eka2l1::driver::emu_window_glfw3 *win = reinterpret_cast<decltype(win)>(glfwGetWindowUserPointer(window));

    CALL_IF_VALID(win->resize_hook, eka2l1::vec2(width, height));
}

void close_callback(GLFWwindow *window) {
    eka2l1::driver::emu_window_glfw3 *win = reinterpret_cast<decltype(win)>(glfwGetWindowUserPointer(window));
    CALL_IF_VALID(win->close_hook);
}

namespace eka2l1 {
    namespace driver {
        const uint32_t default_width_potrait = 360;
        const uint32_t default_height_potrait = 640;

        void emu_window_glfw3::init(std::string title, vec2 size) {
            glfwInit();

            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

            emu_win = glfwCreateWindow(size.x, size.y, title.data(), nullptr, nullptr);

            if (!emu_win) {
                LOG_ERROR("Can't create emulator window!");
            }

            using namespace std::placeholders;

            glfwSetWindowUserPointer(emu_win, this);

            glfwSetKeyCallback(emu_win, &key_callback);
            glfwSetMouseButtonCallback(emu_win, &mouse_callback);
            glfwSetFramebufferSizeCallback(emu_win, &fb_resize_callback);
            glfwSetWindowCloseCallback(emu_win, &close_callback);

            emu_screen_size = size;
        }

        void emu_window_glfw3::change_title(std::string new_title) {
            glfwSetWindowTitle(emu_win, new_title.c_str());
        }

        void emu_window_glfw3::make_current() {
            glfwMakeContextCurrent(emu_win);
        }

        void emu_window_glfw3::done_current() {
            glfwMakeContextCurrent(nullptr);
        }

        void emu_window_glfw3::swap_buffer() {
            glfwSwapBuffers(emu_win);
        }

        void emu_window_glfw3::shutdown() {
            glfwDestroyWindow(emu_win);
            glfwTerminate();
        }

        void emu_window_glfw3::poll_events() {
            glfwPollEvents();
        }
    }
}