/*
 * atri-pcba - port of Yandex quasar_pcba_ids.ko to userspace
 *
 * Vendor driver read board identity EEPROMs over i2c (at24cxx family)
 * and Amlogic "unifykey" storage, then exposed a JSON blob through sysfs
 * (/sys/.../pcba-ids/pcba_ids). We replicate that exactly:
 *
 *   atri-pcba            -> prints JSON like vendor did:
 *                           {"mainboard":"...","led_display":...,
 *                            "led_ring":...,"mics":...,"dc_ethernet":...}
 *   atri-pcba --raw N    -> hexdump eeprom #N content
 *
 * EEPROMs on AtriStation i2c0: 0x50(mainboard) 0x52(led_ring)
 * 0x56(mics); i2c bus enumerates as /dev/i2c-0.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#define I2C_DEV "/dev/i2c-0"

struct { const char *name; uint8_t addr; } eeproms[] = {
    { "mainboard",   0x50 },
    { "led_display", 0x50 }, /* may share, adjusted below if needed */
    { "led_ring",    0x52 },
    { "mics",        0x56 },
};

static int read_eeprom(uint8_t addr, unsigned char *buf, int len)
{
    int fd = open(I2C_DEV, O_RDWR);
    if (fd < 0) { perror("open " I2C_DEV); return -1; }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) { perror("I2C_SLAVE"); close(fd); return -1; }
    /* at24 seek: write one dummy byte = offset 0, then read */
    uint8_t off = 0;
    if (write(fd, &off, 1) != 1) { /* some kernels need this, ignore err */ }
    int got = 0;
    while (got < len) {
        int r = read(fd, buf + got, len - got);
        if (r <= 0) break;
        got += r;
    }
    close(fd);
    return got;
}

/* printable-string helper: trim 0xff padding and NULs */
static void json_str(const unsigned char *d, int n, char *out, int outn)
{
    int s = 0;
    while (s < n && (d[s] == 0 || d[s] == 0xFF)) s++;
    int e = n;
    while (e > s && (d[e-1] == 0 || d[e-1] == 0xFF)) e--;
    int j = 0;
    for (int i = s; i < e && j < outn - 1; i++) {
        unsigned char c = d[i];
        if (c == '"' || c == '\\') { out[j++] = '\\'; }
        if (c >= 0x20 && c < 0x7F) out[j++] = c;
    }
    out[j] = 0;
}

int main(int argc, char **argv)
{
    int raw_idx = -1;
    if (argc >= 3 && !strcmp(argv[1], "--raw"))
        raw_idx = atoi(argv[2]);

    #define LEN 32
    unsigned char data[5][LEN] = {0};
    const char *names[5] = { "mainboard", "led_display", "led_ring",
                             "mics", "dc_ethernet" };
    const uint8_t addrs[5] = { 0x50, 0x50, 0x52, 0x56, 0x51 };

    if (raw_idx >= 0 && raw_idx <= 4) {
        unsigned char b[LEN]; memset(b, 0, sizeof b);
        int g = read_eeprom(addrs[raw_idx], b, LEN);
        printf("# %s @0x%02X (%d bytes)\n", names[raw_idx],
               addrs[raw_idx], g);
        for (int i = 0; i < g; i++) printf("%02x ", b[i]);
        printf("\n");
        return 0;
    }

    printf("{");
    for (int i = 0; i < 5; i++) {
        char val[128];
        read_eeprom(addrs[i], data[i], LEN);
        json_str(data[i], LEN, val, sizeof val);
        printf("%s\"%s\":\"%s\"", i ? "," : "", names[i], val);
    }
    printf("}\n");
    return 0;
}
