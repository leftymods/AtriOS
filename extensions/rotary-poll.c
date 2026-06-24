#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <sys/stat.h>
#include <linux/uinput.h>

static int running = 1;

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

/* Find sysfs GPIO number for a given chip label and offset */
static int find_gpio_number(const char *chip_label, int offset) {
    DIR *d = opendir("/sys/class/gpio");
    if (!d) return -1;

    struct dirent *de;
    char path[256], label[64];
    int base = -1;

    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "gpiochip", 8) != 0) continue;
        snprintf(path, sizeof(path), "/sys/class/gpio/%s/label", de->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        int n = read(fd, label, sizeof(label)-1);
        close(fd);
        if (n < 0) continue;
        label[n] = '\0';
        while (n > 0 && (label[n-1] == '\n' || label[n-1] == ' ')) label[--n] = '\0';
        if (strcmp(label, chip_label) == 0) {
            sscanf(de->d_name + 8, "%d", &base);
            break;
        }
    }
    closedir(d);
    if (base < 0) return -1;
    return base + offset;
}

/* Export GPIO via sysfs */
static int export_gpio(int gpio) {
    char path[64];
    int fd;

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    if (access(path, F_OK) == 0) return gpio;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) return -1;
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, n);
    close(fd);

    usleep(10000);

    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio);
    fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, "in", 2);
    close(fd);

    return gpio;
}

/* Read GPIO value from sysfs */
static int read_gpio(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[4];
    int n = read(fd, buf, sizeof(buf)-1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    return atoi(buf);
}

/* Create uinput device for REL_DIAL */
static int create_uinput(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;

    ioctl(fd, UI_SET_EVBIT, EV_REL);
    ioctl(fd, UI_SET_RELBIT, REL_DIAL);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    strcpy(usetup.name, "AtriStation Rotary Encoder");
    usetup.id.bustype = BUS_VIRTUAL;
    usetup.id.vendor = 0x1;
    usetup.id.product = 0x1;
    usetup.id.version = 1;

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);

    return fd;
}

/* Emit REL_DIAL event */
static void emit_rel(int uinput_fd, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_REL;
    ev.code = REL_DIAL;
    ev.value = value;
    write(uinput_fd, &ev, sizeof(ev));

    memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinput_fd, &ev, sizeof(ev));
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <chip_a_label> <offset_a> <chip_b_label> <offset_b>\n", argv[0]);
        fprintf(stderr, "Example: %s gpio 49 gpio-ao 10\n", argv[0]);
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    const char *chip_a = argv[1];
    int offset_a = atoi(argv[2]);
    const char *chip_b = argv[3];
    int offset_b = atoi(argv[4]);

    int gpio_a = find_gpio_number(chip_a, offset_a);
    int gpio_b = find_gpio_number(chip_b, offset_b);
    if (gpio_a < 0 || gpio_b < 0) {
        fprintf(stderr, "Failed to find GPIO chips (%s/offset=%d, %s/offset=%d)\n",
                chip_a, offset_a, chip_b, offset_b);
        return 1;
    }

    if (export_gpio(gpio_a) < 0 || export_gpio(gpio_b) < 0) {
        fprintf(stderr, "Failed to export GPIOs (%d, %d)\n", gpio_a, gpio_b);
        return 1;
    }

    int uinput_fd = create_uinput();
    if (uinput_fd < 0) {
        fprintf(stderr, "Failed to create uinput device (%s). Is CONFIG_INPUT_UINPUT enabled?\n",
                strerror(errno));
        return 1;
    }

    /* Read initial state */
    int prev_a = read_gpio(gpio_a);
    int prev_b = read_gpio(gpio_b);

    while (running) {
        usleep(10000); /* 10ms poll interval */

        int cur_a = read_gpio(gpio_a);
        int cur_b = read_gpio(gpio_b);
        if (cur_a < 0 || cur_b < 0) continue;

        /* Gray code decoding: detect state change */
        int prev_state = (prev_a << 1) | prev_b;
        int cur_state = (cur_a << 1) | cur_b;

        if (cur_state != prev_state) {
            /* For Gray code:
             * One bit changes at a time.
             * Clockwise: 00→01→11→10→00 (or 00→10→11→01→00 depending on wiring)
             * The sign determines direction.
             *
             * Gray code transitions:
             * prev  cur   direction (for typical wiring)
             * 00 → 01 : +1
             * 01 → 11 : +1
             * 11 → 10 : +1
             * 10 → 00 : +1
             * Other direction: -1 for each
             */
            static const int dir[4][4] = {
                { 0, 1, -1, 0 },  /* from 00 */
                { -1, 0, 1, 0 },  /* from 01 */
                { 0, -1, 0, 1 },  /* from 10 */
                { 1, 0, 0, -1 },  /* from 11 */
            };

            int d = dir[prev_state][cur_state];
            if (d != 0) emit_rel(uinput_fd, d);

            prev_a = cur_a;
            prev_b = cur_b;
        }
    }

    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);

    /* Unexport GPIOs */
    char buf[16];
    int fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        int n = snprintf(buf, sizeof(buf), "%d", gpio_a);
        write(fd, buf, n);
        n = snprintf(buf, sizeof(buf), "%d", gpio_b);
        write(fd, buf, n);
        close(fd);
    }

    return 0;
}
