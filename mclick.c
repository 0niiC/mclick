#include <atomic>
#include <fcntl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <random>
#include <sys/time.h>

using namespace std;

// Verified Constants
const int DEFAULT_HOLD_MS = 120;     // -h: press duration
const int DEFAULT_CLICK_SPEED_MS = 120; // -cs: between clicks
const int DEFAULT_CLICK_COUNT = 1;    // Default clicks

// ANSI Colors
const string COLOR_RESET = "\033[0m";
const string COLOR_RED = "\033[31m";
const string COLOR_GREEN = "\033[32m";
const string COLOR_YELLOW = "\033[33m";
const string COLOR_BLUE = "\033[34m";

// Global state with thread safety
atomic<bool> debug_mode{false};
atomic<int> click_speed_ms{DEFAULT_CLICK_SPEED_MS};

int setup_uinput_device() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        cerr << COLOR_RED 
             << "[ERROR] Failed to open uinput device: " << strerror(errno)
             << endl << COLOR_GREEN
             << "[HELP] Try running with sudo"
             << COLOR_RESET << endl;
        exit(EXIT_FAILURE);
    }

    // Validate ioctl operations
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT) < 0 ||
        ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT) < 0) {
        cerr << COLOR_RED << "[ERROR] Failed to set device capabilities" << COLOR_RESET << endl;
        close(fd);
        exit(EXIT_FAILURE);
    }

    struct uinput_user_dev dev = {0};
    snprintf(dev.name, UINPUT_MAX_NAME_SIZE, "virtual-mouse");
    dev.id.bustype = BUS_USB;
    dev.id.vendor = 0x1234;
    dev.id.product = 0x5678;
    dev.id.version = 1;

    if (write(fd, &dev, sizeof(dev)) != sizeof(dev)) {
        cerr << COLOR_RED 
             << "[ERROR] Failed to write device info: " << strerror(errno)
             << COLOR_RESET << endl;
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        cerr << COLOR_RED 
             << "[ERROR] Failed to create device: " << strerror(errno)
             << COLOR_RESET << endl;
        close(fd);
        exit(EXIT_FAILURE);
    }

    return fd;
}

void send_input_event(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    gettimeofday(&ie.time, NULL);
    ie.type = type;
    ie.code = code;
    ie.value = value;

    if (write(fd, &ie, sizeof(ie)) < 0) {
        cerr << COLOR_RED 
             << "[ERROR] Failed to send event: " << strerror(errno)
             << COLOR_RESET << endl;
        return;
    }

    if (debug_mode && type == EV_KEY) {
        auto now = chrono::system_clock::now();
        time_t time_point = chrono::system_clock::to_time_t(now);
        auto ms = chrono::duration_cast<chrono::milliseconds>(
            now.time_since_epoch() % chrono::seconds(1));
        
        cout << COLOR_YELLOW << "[DEBUG] " << COLOR_GREEN
             << put_time(localtime(&time_point), "%H:%M:%S:") << ms.count()
             << " " << (value ? "Press" : "Release") << " code=" << code
             << COLOR_RESET << endl;
    }
}

void send_event(int fd, int button, int action) {
    send_input_event(fd, EV_KEY, button, action);
    send_input_event(fd, EV_SYN, SYN_REPORT, 0);
}

void sleep_ms(int ms) {
    this_thread::sleep_for(chrono::milliseconds(ms));
}

auto now() {
    return chrono::steady_clock::now();
}

bool has_option(int argc, char* argv[], const char* option) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], option) == 0) return true;
    }
    return false;
}

int get_count(int argc, char* argv[]) {
    for (int i = 0; i < argc; i++) {
        string arg = argv[i];
        if (isdigit(arg[0])) return stoi(arg);
    }
    return DEFAULT_CLICK_COUNT;
}

// Forward declarations
int parse_duration(const char* duration_str);
int get_duration(int argc, char* argv[], const string& option);

int get_duration(int argc, char* argv[], const string& option) {
    for (int i = 0; i < argc; i++) {
        if (option == argv[i] && i + 1 < argc) {
            return parse_duration(argv[i + 1]);
        }
    }
    return 0;
}

int parse_duration(const char* duration_str) {
    try {
        string s = duration_str;
        size_t suffix_pos = s.find_first_not_of("0123456789");
        
        int value = stoi(s);
        if (value <= 0) throw invalid_argument("Duration must be positive");
        
        // Handle 's' suffix for seconds
        if (suffix_pos != string::npos && s[suffix_pos] == 's') {
            return value * 1000; // Convert to milliseconds
        }
        return value;
    } catch (...) {
        cerr << COLOR_RED << "[ERROR] Invalid duration: " << duration_str << COLOR_RESET << endl;
        exit(EXIT_FAILURE);
    }
}

void perform_clicks(int fd, int button, int count, int hold_ms, int click_speed_ms) {
    for (int i = 0; i < count; i++) {
        // Press-hold-release with verified timing
        send_event(fd, button, 1);
        sleep_ms(hold_ms);
        send_event(fd, button, 0);
        
        if (i < count - 1) sleep_ms(click_speed_ms);
    }
}

void perform_timed_clicks(int fd, int button, int duration_ms, int hold_ms, int click_speed_ms) {
    if (debug_mode) {
        cout << COLOR_YELLOW << "[DEBUG] Timed clicks: " << duration_ms << "ms" 
             << " (hold=" << hold_ms << "ms, speed=" << click_speed_ms << "ms)" 
             << COLOR_RESET << endl;
    }

    auto start = chrono::steady_clock::now();
    while (chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - start).count() < duration_ms) {
        
        // Press-hold-release cycle
        send_event(fd, button, 1);
        sleep_ms(hold_ms);
        send_event(fd, button, 0);
        
        // Wait between cycles
        sleep_ms(click_speed_ms);
    }
}

void print_help(const char* program_name) {
    cout << "Mouse click automation\n\n"
         << "Usage: " << program_name << " [l/r] [options]\n\n"
         << "Click options:\n"
         << "  -h, --hold <ms>        Hold duration (default " << DEFAULT_HOLD_MS << "ms)\n"
         << "  -cs, --clickspeed <ms> Delay between clicks\n"
         << "  -t, --time <ms>        Continuous click duration\n\n"
         << "Other options:\n"
         << "  -d, --debug            Enable verbose output\n";
}

int main(int argc, char* argv[]) {
    const unordered_map<char, int> BUTTONS = {{'l', BTN_LEFT}, {'r', BTN_RIGHT}};

    if (argc < 2 || string(argv[1]) == "-h" || string(argv[1]) == "--help") {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    char button_char = argv[1][0];
    if (BUTTONS.find(button_char) == BUTTONS.end()) {
        cerr << COLOR_RED << "[ERROR] Invalid button: " << button_char << endl
             << "[HELP] Use 'l' or 'r'" << COLOR_RESET << endl;
        return EXIT_FAILURE;
    }
    
    int button = BUTTONS.at(button_char);
    int count = get_count(argc, argv);
    int hold_ms = DEFAULT_HOLD_MS;
    int duration_ms = 0;

    for (int i = 2; i < argc; ++i) {
        string arg = argv[i];
        
        if (arg == "-d" || arg == "--debug") {
            debug_mode = true;
            cout << COLOR_BLUE << "[DEBUG] Debug mode enabled" << COLOR_RESET << endl;
        } 
        else if ((arg == "-h" || arg == "--hold") && i + 1 < argc) {
            hold_ms = parse_duration(argv[++i]);
        }
        else if ((arg == "-cs" || arg == "--clickspeed") && i + 1 < argc) {
            click_speed_ms.store(parse_duration(argv[++i]));
        }
        else if ((arg == "-t" || arg == "--time") && i + 1 < argc) {
            duration_ms = get_duration(argc, argv, arg);
        }
    }

    int fd = setup_uinput_device();
    
    try {
        if (duration_ms > 0) {
            perform_timed_clicks(fd, button, duration_ms, hold_ms, click_speed_ms.load());
        } else {
            perform_clicks(fd, button, count, hold_ms, click_speed_ms.load());
        }
    } catch (const exception& e) {
        cerr << COLOR_RED << "[ERROR] " << e.what() << COLOR_RESET << endl;
        close(fd);
        return EXIT_FAILURE;
    }
    
    close(fd);
    return EXIT_SUCCESS;
}