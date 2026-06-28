#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DIAG_VERNO_F 0x00
#define DIAG_LOG_F 0x10
#define DIAG_EVENT_REPORT_F 0x60
#define DIAG_LOG_CONFIG_F 0x73
#define DIAG_EXT_BUILD_ID_F 0x7c
#define LOG_CONFIG_DISABLE_OP 0
#define LOG_CONFIG_RETRIEVE_ID_RANGES_OP 1
#define LOG_CONFIG_SET_MASK_OP 3
#define DIAG_SUBSYS_ID_LTE 0x0b

#define LOG_LTE_SMEAS 0xB17F
#define LOG_LTE_NMEAS 0xB180
#define LOG_LTE_CELL_INFO 0xB197
#define LOG_LTE_RRC_SCELL 0xB0C2
#define LOG_LTE_CA 0xB0CD
#define LOG_NR_RRC_SCELL 0xB823
#define LOG_NR_CA 0xB826
#define LOG_NR_ML1 0xB97F

static volatile sig_atomic_t running = 1;

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static uint16_t dm_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1) crc = (uint16_t)((crc >> 1) ^ 0x8408);
            else crc >>= 1;
        }
    }
    return (uint16_t)(crc ^ 0xffff);
}

static double rsrp(uint32_t raw) { return -180.0 + (double)raw * 0.0625; }
static double rsrq(uint32_t raw) { return -30.0 + (double)raw * 0.0625; }
static double rssi(uint32_t raw) { return -110.0 + (double)raw * 0.0625; }

static void hexprint(FILE *out, const uint8_t *buf, size_t len) {
    static const char h[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        fputc(h[buf[i] >> 4], out);
        fputc(h[buf[i] & 0xf], out);
    }
}

static int send_packet(int fd, const uint8_t *payload, size_t len) {
    uint8_t tmp[4096];
    uint8_t out[8192];
    if (len + 2 > sizeof(tmp)) return -1;
    memcpy(tmp, payload, len);
    uint16_t crc = dm_crc16(payload, len);
    tmp[len++] = crc & 0xff;
    tmp[len++] = crc >> 8;
    size_t opos = 0;
    for (size_t i = 0; i < len; i++) {
        if (opos + 2 >= sizeof(out)) return -1;
        if (tmp[i] == 0x7d) {
            out[opos++] = 0x7d; out[opos++] = 0x5d;
        } else if (tmp[i] == 0x7e) {
            out[opos++] = 0x7d; out[opos++] = 0x5e;
        } else {
            out[opos++] = tmp[i];
        }
    }
    out[opos++] = 0x7e;
    return write(fd, out, opos) == (ssize_t)opos ? 0 : -1;
}

static size_t unescape(const uint8_t *in, size_t len, uint8_t *out, size_t outlen) {
    size_t opos = 0;
    for (size_t i = 0; i < len && opos < outlen; i++) {
        if (in[i] == 0x7d && i + 1 < len) {
            i++;
            if (in[i] == 0x5e) out[opos++] = 0x7e;
            else if (in[i] == 0x5d) out[opos++] = 0x7d;
            else out[opos++] = in[i];
        } else {
            out[opos++] = in[i];
        }
    }
    return opos;
}

static void set_bit(uint8_t *mask, uint32_t item) {
    mask[item / 8] |= (uint8_t)(1u << (item % 8));
}

static int send_log_mask(int fd, int mac) {
    uint8_t pkt[16 + 320];
    memset(pkt, 0, sizeof(pkt));
    uint32_t vals[4] = {DIAG_LOG_CONFIG_F, LOG_CONFIG_SET_MASK_OP, DIAG_SUBSYS_ID_LTE, 0x09ff};
    memcpy(pkt, vals, sizeof(vals));
    uint8_t *m = pkt + 16;
    uint32_t items[] = {
        0x17f, 0x180, 0x193, 0x197, 0x0c1, 0x0c2, 0x0cd,
        0x822, 0x823, 0x825, 0x826, 0x97f,
    };
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++) set_bit(m, items[i]);
    if (mac) {
        set_bit(m, 0x063);
        set_bit(m, 0x064);
    }
    return send_packet(fd, pkt, sizeof(pkt));
}

static void send_init(int fd, int mac) {
    uint8_t p1[] = {DIAG_VERNO_F};
    uint8_t p2[] = {DIAG_EXT_BUILD_ID_F};
    uint8_t ev0[] = {DIAG_EVENT_REPORT_F, 0};
    uint32_t ranges[] = {DIAG_LOG_CONFIG_F, LOG_CONFIG_RETRIEVE_ID_RANGES_OP};
    uint8_t ev1[] = {DIAG_EVENT_REPORT_F, 1};
    send_packet(fd, p1, sizeof(p1));
    send_packet(fd, p2, sizeof(p2));
    send_packet(fd, ev0, sizeof(ev0));
    send_packet(fd, (uint8_t *)ranges, sizeof(ranges));
    send_log_mask(fd, mac);
    send_packet(fd, ev1, sizeof(ev1));
}

static void send_stop(int fd) {
    uint8_t ev0[] = {DIAG_EVENT_REPORT_F, 0};
    uint32_t disable[] = {DIAG_LOG_CONFIG_F, LOG_CONFIG_DISABLE_OP};
    send_packet(fd, ev0, sizeof(ev0));
    send_packet(fd, (uint8_t *)disable, sizeof(disable));
}

static void json_prefix(uint16_t log_id, uint64_t qts, const char *rat, const char *event) {
    printf("{\"event\":\"%s\",\"rat\":\"%s\",\"log_id\":\"0x%04X\",\"qxdm_ts\":%llu", event, rat, log_id, (unsigned long long)qts);
}

static void write_combo(const char *dir, const char *name, const uint8_t *body, size_t len) {
    if (!dir) return;
    mkdir(dir, 0755);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return;
    hexprint(f, body, len);
    fputc('\n', f);
    fclose(f);
}

static int parse_lte_smeas(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 36) return 0;
    uint8_t ver = b[0], rrc = b[1];
    uint32_t earfcn, pci_prio, meas_rsrp, avg_rsrp, rsrq_word, rssi_word;
    if (ver == 4) {
        if (len < 32) return 0;
        earfcn = get16(b + 4);
        pci_prio = get16(b + 6);
        meas_rsrp = get32(b + 8);
        avg_rsrp = get32(b + 12);
        rsrq_word = get32(b + 16);
        rssi_word = get32(b + 20);
    } else if (ver == 5) {
        earfcn = get32(b + 4);
        pci_prio = get16(b + 8);
        meas_rsrp = get32(b + 12);
        avg_rsrp = get32(b + 16);
        rsrq_word = get32(b + 20);
        rssi_word = get32(b + 24);
    } else return 0;
    json_prefix(log_id, qts, "LTE", "serving_cell_measurement");
    printf(",\"version\":%u,\"rrc_release\":%u,\"earfcn\":%u,\"pci\":%u,"
           "\"rsrp_dbm\":%.2f,\"avg_rsrp_dbm\":%.2f,\"rssi_dbm\":%.2f,"
           "\"rsrq_db\":%.2f,\"avg_rsrq_db\":%.2f}\n",
           ver, rrc, earfcn, pci_prio & 0x1ff,
           rsrp(meas_rsrp & 0xfff), rsrp(avg_rsrp & 0xfff),
           rssi((rssi_word >> 10) & 0x7ff), rsrq(rsrq_word & 0x3ff),
           rsrq((rsrq_word >> 20) & 0x3ff));
    return 1;
}

static int parse_lte_rrc_scell(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 29) return 0;
    uint8_t ver = b[0];
    uint16_t pci, dlbw, ulbw, tac, mcc, mnc;
    uint32_t dl, ul, cid, band;
    uint8_t mncd;
    if (ver == 2) {
        pci = get16(b + 1); dl = get16(b + 3); ul = get16(b + 5);
        dlbw = b[7]; ulbw = b[8]; cid = get32(b + 9); tac = get16(b + 13);
        band = get32(b + 15); mcc = get16(b + 19); mncd = b[21]; mnc = get16(b + 22);
    } else if (ver == 3) {
        pci = get16(b + 1); dl = get32(b + 3); ul = get32(b + 7);
        dlbw = b[11]; ulbw = b[12]; cid = get32(b + 13); tac = get16(b + 17);
        band = get32(b + 19); mcc = get16(b + 23); mncd = b[25]; mnc = get16(b + 26);
    } else return 0;
    json_prefix(log_id, qts, "LTE", "serving_cell_info");
    printf(",\"version\":%u,\"pci\":%u,\"dl_earfcn\":%u,\"ul_earfcn\":%u,"
           "\"band\":%u,\"dl_bandwidth_prb\":%u,\"ul_bandwidth_prb\":%u,"
           "\"cell_id\":%u,\"tac\":%u,\"mcc\":%u,\"mnc\":\"%0*u\"}\n",
           ver, pci, dl, ul, band, dlbw, ulbw, cid, tac, mcc, mncd == 3 ? 3 : 2, mnc);
    return 1;
}

static int parse_nr_rrc_scell(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 46) return 0;
    uint16_t rel_min = get16(b), rel_maj = get16(b + 2);
    size_t off;
    int has_cgi = 1;
    if (rel_maj == 0 && rel_min == 4) { off = 4; has_cgi = 0; }
    else if (rel_maj == 3 && rel_min == 0) off = 4;
    else if (rel_maj == 3 && (rel_min == 2 || rel_min == 3)) off = 7;
    else return 0;
    uint16_t pci = get16(b + off);
    if (!has_cgi) {
        uint32_t dl = get32(b + off + 2), ul = get32(b + off + 6);
        uint16_t dlbw = get16(b + off + 10), ulbw = get16(b + off + 12);
        uint64_t cid = get64(b + off + 14);
        uint16_t mcc = get16(b + off + 22), mnc = get16(b + off + 25);
        uint8_t mncd = b[off + 24];
        uint32_t tac = get32(b + off + 28);
        uint16_t band = get16(b + off + 32);
        json_prefix(log_id, qts, "NR", "serving_cell_info");
        printf(",\"version\":\"%u.%u\",\"pci\":%u,\"dl_nr_arfcn\":%u,\"ul_nr_arfcn\":%u,"
               "\"band\":%u,\"dl_bandwidth_mhz\":%u,\"ul_bandwidth_mhz\":%u,"
               "\"cell_id\":%llu,\"tac\":%u,\"mcc\":%u,\"mnc\":\"%0*u\"}\n",
               rel_maj, rel_min, pci, dl, ul, band, dlbw, ulbw,
               (unsigned long long)cid, tac, mcc, mncd == 3 ? 3 : 2, mnc);
    } else {
        uint64_t ncgi = get64(b + off + 2);
        uint32_t dl = get32(b + off + 10), ul = get32(b + off + 14);
        uint16_t dlbw = get16(b + off + 18), ulbw = get16(b + off + 20);
        uint64_t cid = get64(b + off + 22);
        uint16_t mcc = get16(b + off + 30), mnc = get16(b + off + 33);
        uint8_t mncd = b[off + 32];
        uint32_t tac = get32(b + off + 36);
        uint16_t band = get16(b + off + 40);
        json_prefix(log_id, qts, "NR", "serving_cell_info");
        printf(",\"version\":\"%u.%u\",\"pci\":%u,\"nr_cgi\":%llu,\"dl_nr_arfcn\":%u,\"ul_nr_arfcn\":%u,"
               "\"band\":%u,\"dl_bandwidth_mhz\":%u,\"ul_bandwidth_mhz\":%u,"
               "\"cell_id\":%llu,\"tac\":%u,\"mcc\":%u,\"mnc\":\"%0*u\"}\n",
               rel_maj, rel_min, pci, (unsigned long long)ncgi, dl, ul, band, dlbw, ulbw,
               (unsigned long long)cid, tac, mcc, mncd == 3 ? 3 : 2, mnc);
    }
    return 1;
}

static void parse_log(const uint8_t *pkt, size_t len, int debug, const char *combo_dir, int *got_lte, int *got_nr) {
    if (len < 16 || pkt[0] != DIAG_LOG_F) return;
    uint16_t log_id = get16(pkt + 6);
    uint64_t qts = get64(pkt + 8);
    const uint8_t *body = pkt + 16;
    size_t body_len = len - 16;
    int parsed = 0;
    if (log_id == LOG_LTE_SMEAS) parsed = parse_lte_smeas(log_id, qts, body, body_len);
    else if (log_id == LOG_LTE_RRC_SCELL) parsed = parse_lte_rrc_scell(log_id, qts, body, body_len);
    else if (log_id == LOG_NR_RRC_SCELL) parsed = parse_nr_rrc_scell(log_id, qts, body, body_len);
    else if (log_id == LOG_LTE_CA) {
        *got_lte = 1;
        write_combo(combo_dir, "b0cd_qlte.hex", body, body_len);
        json_prefix(log_id, qts, "LTE", "supported_ca_combos_raw");
        printf(",\"format\":\"QLTE\",\"payload_hex\":\""); hexprint(stdout, body, body_len); printf("\"}\n");
        parsed = 1;
    } else if (log_id == LOG_NR_CA) {
        *got_nr = 1;
        write_combo(combo_dir, "b826_qnr.hex", body, body_len);
        json_prefix(log_id, qts, "NR", "supported_ca_combos_raw");
        printf(",\"format\":\"QNR\",\"payload_hex\":\""); hexprint(stdout, body, body_len); printf("\"}\n");
        parsed = 1;
    }
    if (!parsed && debug) {
        printf("{\"event\":\"diag_log_raw\",\"log_id\":\"0x%04X\",\"qxdm_ts\":%llu,\"body_len\":%zu,\"body_hex\":\"",
               log_id, (unsigned long long)qts, body_len);
        hexprint(stdout, body, body_len);
        printf("\"}\n");
    }
    fflush(stdout);
}

static int process_frame(const uint8_t *frame, size_t flen, int debug, const char *combo_dir, int *got_lte, int *got_nr) {
    uint8_t pkt[8192];
    size_t len = unescape(frame, flen, pkt, sizeof(pkt));
    if (len < 3) return 0;
    uint16_t got = get16(pkt + len - 2);
    uint16_t want = dm_crc16(pkt, len - 2);
    if (got != want) {
        if (debug) printf("{\"event\":\"frame_error\",\"reason\":\"crc\",\"got\":%u,\"expected\":%u}\n", got, want);
        return 0;
    }
    len -= 2;
    if (len && pkt[0] == DIAG_LOG_F) parse_log(pkt, len, debug, combo_dir, got_lte, got_nr);
    else if (debug && len) {
        printf("{\"event\":\"diag_control\",\"cmd\":\"0x%02X\",\"payload_hex\":\"", pkt[0]);
        hexprint(stdout, pkt, len);
        printf("\"}\n");
    }
    return 1;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [-d /dev/smd11] [--seconds N] [--no-configure] [--stop] [--debug]\n"
        "          [--combo-dir DIR] [--wait-uecap lte|nr|both] [--mac]\n", argv0);
}

int main(int argc, char **argv) {
    const char *dev = "/dev/smd11";
    const char *combo_dir = NULL;
    const char *wait = NULL;
    int configure = 1, stop = 0, debug = 0, mac = 0;
    int seconds = 0;
    static struct option opts[] = {
        {"device", required_argument, 0, 'd'},
        {"seconds", required_argument, 0, 's'},
        {"no-configure", no_argument, 0, 1},
        {"stop", no_argument, 0, 2},
        {"debug", no_argument, 0, 3},
        {"combo-dir", required_argument, 0, 4},
        {"wait-uecap", required_argument, 0, 5},
        {"mac", no_argument, 0, 6},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "d:s:h", opts, NULL)) != -1) {
        if (c == 'd') dev = optarg;
        else if (c == 's') seconds = atoi(optarg);
        else if (c == 1) configure = 0;
        else if (c == 2) stop = 1;
        else if (c == 3) debug = 1;
        else if (c == 4) combo_dir = optarg;
        else if (c == 5) wait = optarg;
        else if (c == 6) mac = 1;
        else { usage(argv[0]); return c == 'h' ? 0 : 2; }
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGALRM, on_signal);
    if (seconds > 0) alarm((unsigned int)seconds + 1);
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "open %s failed: %s\n", dev, strerror(errno));
        return 1;
    }
    if (configure) send_init(fd, mac);
    uint8_t readbuf[4096], frame[8192];
    size_t fpos = 0;
    time_t start = time(NULL);
    int got_lte = 0, got_nr = 0;
    while (running) {
        if (seconds > 0 && time(NULL) - start >= seconds) break;
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
        int rv = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (rv <= 0) continue;
        ssize_t n = read(fd, readbuf, sizeof(readbuf));
        if (n <= 0) continue;
        for (ssize_t i = 0; i < n; i++) {
            if (readbuf[i] == 0x7e) {
                if (fpos > 0) process_frame(frame, fpos, debug, combo_dir, &got_lte, &got_nr);
                fpos = 0;
            } else if (fpos < sizeof(frame)) {
                frame[fpos++] = readbuf[i];
            } else {
                fpos = 0;
            }
        }
        if (wait) {
            if ((!strcmp(wait, "lte") && got_lte) || (!strcmp(wait, "nr") && got_nr) ||
                (!strcmp(wait, "both") && got_lte && got_nr)) break;
        }
    }
    if (stop) send_stop(fd);
    close(fd);
    return 0;
}
