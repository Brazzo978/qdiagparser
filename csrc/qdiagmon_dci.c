#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef unsigned char boolean;
typedef unsigned char byte;
typedef uint16_t uint16;
typedef uint16_t diag_dci_peripherals;

boolean Diag_LSM_Init(byte *pIEnv);
boolean Diag_LSM_DeInit(void);
int diag_register_dci_client(int *client_id, diag_dci_peripherals *list, int proc, void *os_params);
int diag_get_dci_support_list_proc(int proc, diag_dci_peripherals *list);
int diag_register_dci_signal_data(int client_id, int signal_type);
int diag_deregister_dci_signal_data(int client_id, int signal_type);
int diag_register_dci_stream_proc(int client_id,
                                  void (*log_cb)(unsigned char *ptr, int len),
                                  void (*event_cb)(unsigned char *ptr, int len));
int diag_log_stream_config(int client_id, int set_mask, uint16 log_codes_array[], int num_codes);
int diag_disable_all_logs(int client_id);
int diag_dci_vote_real_time(int client_id, int real_time);
int diag_release_dci_client(int *client_id);
extern int diag_disable_console;

#define DIAG_DCI_NO_ERROR 1001
#define DIAG_DCI_NOT_SUPPORTED 1004
#define DIAG_CON_APSS 0x0001
#define DIAG_CON_MPSS 0x0002
#define DIAG_CON_LPASS 0x0004
#define DIAG_CON_WCNSS 0x0008
#define ENABLE 1
#define DISABLE 0
#define DIAG_PROC_MSM 0
#define DIAG_PROC_MDM 1
#define MODE_REALTIME 1
#define LOG_LTE_SMEAS 0xB17F
#define LOG_LTE_NMEAS 0xB180
#define LOG_LTE_SMEAS_RESP 0xB193
#define LOG_LTE_CELL_INFO 0xB197
#define LOG_LTE_ML1_MAC_RAR_MSG1 0xB167
#define LOG_LTE_ML1_MAC_RAR_MSG2 0xB168
#define LOG_LTE_ML1_MAC_RAR_MSG3 0xB169
#define LOG_LTE_ML1_MAC_RAR_MSG4 0xB16A
#define LOG_LTE_ML1_CONNECTED_INTRA_FREQ 0xB179
#define LOG_LTE_ML1_NEIGHBOR_REQ_RESP 0xB192
#define LOG_LTE_ML1_SEARCH_REQ_RESP 0xB194
#define LOG_LTE_ML1_CONNECTED_NEIGHBOR_REQ_RESP 0xB195
#define LOG_LTE_PHY_PDSCH_DEMAPPER_CONFIG 0xB126
#define LOG_LTE_PHY_PDCCH_DECODING_RESULT 0xB130
#define LOG_LTE_PHY_PDCCH_DECODING_RESULT2 0xB132
#define LOG_LTE_PHY_PUSCH_TX_REPORT 0xB139
#define LOG_LTE_PHY_PUCCH_TX_REPORT 0xB13C
#define LOG_LTE_PHY_PUSCH_CSF_REPORT 0xB140
#define LOG_LTE_PHY_PDSCH_STAT_INDICATION 0xB144
#define LOG_LTE_PHY_PDCCH_PHICH_INDICATION 0xB16B
#define LOG_LTE_PHY_GM_TX_REPORT 0xB16D
#define LOG_LTE_PHY_PDSCH_STAT_INDICATION2 0xB173
#define LOG_LTE_PHY_PUSCH_STAT_INDICATION 0xB174
#define LOG_LTE_PHY_PUCCH_CSF_REPORT 0xB175
#define LOG_LTE_PHY_CQI_REPORT 0xB176
#define LOG_LTE_PHY_RI_REPORT 0xB177
#define LOG_LTE_PHY_PMI_REPORT 0xB178
#define LOG_LTE_RRC_OTA 0xB0C0
#define LOG_LTE_RRC_MIB 0xB0C1
#define LOG_LTE_RRC_SCELL 0xB0C2
#define LOG_LTE_CA 0xB0CD
#define LOG_LTE_MAC_DL 0xB063
#define LOG_LTE_MAC_UL 0xB064
#define LOG_NR_NAS_5GMM_STATE 0xB80C
#define LOG_NR_RRC_OTA 0xB821
#define LOG_NR_MIB 0xB822
#define LOG_NR_RRC_SCELL 0xB823
#define LOG_NR_RRC_CFG 0xB825
#define LOG_NR_CA 0xB826
#define LOG_NR_MAC_RACH_ATTEMPT 0xB88A
#define LOG_NR_ML1 0xB97F

static volatile sig_atomic_t running = 1;
static int opt_debug = 0;
static int opt_raw = 0;
static int opt_probe_scheduling = 0;
static int opt_probe_phy = 0;
static const char *combo_dir = NULL;
static int got_lte = 0;
static int got_nr = 0;

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | ((uint64_t)get32(p + 4) << 32);
}

static double rsrp(uint32_t raw) { return -180.0 + (double)raw * 0.0625; }
static double rsrq(uint32_t raw) { return -30.0 + (double)raw * 0.0625; }
static double rssi(uint32_t raw) { return -110.0 + (double)raw * 0.0625; }
static double snr_lte(uint32_t raw) { return (double)raw * 0.1 - 20.0; }

static uint32_t getbits32(const uint32_t *words, size_t n_words, unsigned int start, unsigned int len) {
    if (!len || len > 32) return 0;
    size_t wi = start / 32;
    unsigned int shift = start % 32;
    if (wi >= n_words) return 0;
    uint64_t acc = (uint64_t)words[wi] >> shift;
    if (shift && wi + 1 < n_words) acc |= (uint64_t)words[wi + 1] << (32 - shift);
    if (len == 32) return (uint32_t)acc;
    return (uint32_t)(acc & ((1ULL << len) - 1));
}

static uint32_t bitu32(uint32_t value, unsigned int start, unsigned int len) {
    if (!len || len >= 32) return value >> start;
    return (value >> start) & ((1U << len) - 1U);
}

static void on_signal(int sig) {
    (void)sig;
    running = 0;
}

static void on_dci_signal(int sig) {
    (void)sig;
}

static void hexprint(FILE *out, const uint8_t *buf, size_t len) {
    static const char h[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        fputc(h[buf[i] >> 4], out);
        fputc(h[buf[i] & 0xf], out);
    }
}

static void json_prefix(uint16_t log_id, uint64_t qts, const char *rat, const char *event) {
    printf("{\"event\":\"%s\",\"rat\":\"%s\",\"log_id\":\"0x%04X\",\"qxdm_ts\":%llu",
           event, rat, log_id, (unsigned long long)qts);
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

static int is_probe_scheduling_log(uint16_t log_id) {
    switch (log_id) {
        case LOG_LTE_ML1_MAC_RAR_MSG1:
        case LOG_LTE_ML1_MAC_RAR_MSG2:
        case LOG_LTE_ML1_MAC_RAR_MSG3:
        case LOG_LTE_ML1_MAC_RAR_MSG4:
        case LOG_LTE_ML1_CONNECTED_INTRA_FREQ:
        case LOG_LTE_ML1_NEIGHBOR_REQ_RESP:
        case LOG_LTE_ML1_SEARCH_REQ_RESP:
        case LOG_LTE_ML1_CONNECTED_NEIGHBOR_REQ_RESP:
        case LOG_NR_MAC_RACH_ATTEMPT:
            return 1;
        default:
            return 0;
    }
}

static int is_probe_phy_log(uint16_t log_id) {
    switch (log_id) {
        case LOG_LTE_PHY_PDSCH_DEMAPPER_CONFIG:
        case LOG_LTE_PHY_PDCCH_DECODING_RESULT:
        case LOG_LTE_PHY_PDCCH_DECODING_RESULT2:
        case LOG_LTE_PHY_PUSCH_TX_REPORT:
        case LOG_LTE_PHY_PUCCH_TX_REPORT:
        case LOG_LTE_PHY_PUSCH_CSF_REPORT:
        case LOG_LTE_PHY_PDSCH_STAT_INDICATION:
        case LOG_LTE_PHY_PDCCH_PHICH_INDICATION:
        case LOG_LTE_PHY_GM_TX_REPORT:
        case LOG_LTE_PHY_PDSCH_STAT_INDICATION2:
        case LOG_LTE_PHY_PUSCH_STAT_INDICATION:
        case LOG_LTE_PHY_PUCCH_CSF_REPORT:
        case LOG_LTE_PHY_CQI_REPORT:
        case LOG_LTE_PHY_RI_REPORT:
        case LOG_LTE_PHY_PMI_REPORT:
            return 1;
        default:
            return 0;
    }
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

static int parse_lte_smeas_resp(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 20 || b[0] != 1) return 0;
    size_t pos = 4;
    int parsed = 0;
    for (unsigned int i = 0; i < b[1] && pos + 4 <= len; i++) {
        uint8_t sub_id = b[pos], sub_ver = b[pos + 1];
        uint16_t sub_size = get16(b + pos + 2);
        if (sub_size < 4 || pos + sub_size > len) break;
        const uint8_t *sub = b + pos + 4;
        size_t sub_len = sub_size - 4;
        pos += sub_size;
        if (sub_id != 0x19) continue;
        if ((sub_ver == 36 || sub_ver == 48 || sub_ver == 50) && sub_len >= 8) {
            uint32_t earfcn;
            uint16_t num_cells, valid_rx;
            size_t cell_pos, cell_size, snr_offset, cinr_offset;
            uint32_t rx_map = 0;
            if (sub_ver == 36) {
                earfcn = get32(sub);
                num_cells = get16(sub + 4);
                valid_rx = get16(sub + 6);
                cell_pos = 8; cell_size = 128; snr_offset = 80; cinr_offset = 104;
            } else {
                if (sub_len < 12) continue;
                earfcn = get32(sub);
                num_cells = get16(sub + 4);
                valid_rx = get16(sub + 6);
                rx_map = get32(sub + 8);
                cell_pos = 12; cell_size = 140; snr_offset = 92; cinr_offset = 116;
            }
            for (uint16_t idx = 0; idx < num_cells && cell_pos + cinr_offset + 24 <= sub_len; idx++, cell_pos += cell_size) {
                const uint8_t *cell = sub + cell_pos;
                uint16_t meta0 = get16(cell);
                uint16_t meta2 = get16(cell + 4);
                uint32_t words[12];
                uint32_t snr_words[2];
                for (int w = 0; w < 12; w++) words[w] = get32(cell + 16 + (size_t)w * 4);
                snr_words[0] = get32(cell + snr_offset);
                snr_words[1] = get32(cell + snr_offset + 4);
                int32_t prj_sir = (int32_t)get32(cell + cinr_offset);
                uint32_t post_ic_rsrq = get32(cell + cinr_offset + 4);
                int32_t cinr0 = (int32_t)get32(cell + cinr_offset + 8);
                int32_t cinr1 = (int32_t)get32(cell + cinr_offset + 12);
                int32_t cinr2 = (int32_t)get32(cell + cinr_offset + 16);
                int32_t cinr3 = (int32_t)get32(cell + cinr_offset + 20);
                json_prefix(log_id, qts, "LTE", "per_antenna_measurement");
                printf(",\"version\":%u,\"earfcn\":%u,\"cell_index\":%u,\"valid_rx\":%u,\"rx_map\":%u,"
                       "\"pci\":%u,\"serving_cell_index\":%u,\"is_serving_cell\":%s,"
                       "\"sfn\":%u,\"subframe\":%u,"
                       "\"rsrp_dbm\":[%.2f,%.2f,%.2f,%.2f],"
                       "\"combined_rsrp_dbm\":%.2f,\"filtered_rsrp_dbm\":%.2f,"
                       "\"rsrq_db\":[%.2f,%.2f,%.2f,%.2f],"
                       "\"combined_rsrq_db\":%.2f,\"filtered_rsrq_db\":%.2f,"
                       "\"rssi_dbm\":[%.2f,%.2f,%.2f,%.2f],\"combined_rssi_dbm\":%.2f,"
                       "\"snr_db\":[%.2f,%.2f,%.2f,%.2f],"
                       "\"projected_sir_db\":%.2f,\"post_ic_rsrq_db\":%.2f,"
                       "\"cinr_raw\":[%d,%d,%d,%d]}\n",
                       sub_ver, earfcn, idx, valid_rx, rx_map,
                       meta0 & 0x1ff, (meta0 >> 9) & 7, (meta0 & 0x1000) ? "true" : "false",
                       meta2 & 0x3ff, (meta2 >> 10) & 0xf,
                       rsrp(getbits32(words, 12, 10, 12)), rsrp(getbits32(words, 12, 44, 12)),
                       rsrp(getbits32(words, 12, 76, 12)), rsrp(getbits32(words, 12, 96, 12)),
                       rsrp(getbits32(words, 12, 108, 12)) + 40.0, rsrp(getbits32(words, 12, 140, 12)),
                       rsrq(getbits32(words, 12, 160, 10)), rsrq(getbits32(words, 12, 180, 10)),
                       rsrq(getbits32(words, 12, 202, 10)), rsrq(getbits32(words, 12, 212, 10)),
                       rsrq(getbits32(words, 12, 224, 10)), rsrq(getbits32(words, 12, 244, 10)),
                       rssi(getbits32(words, 12, 256, 11)), rssi(getbits32(words, 12, 267, 11)),
                       rssi(getbits32(words, 12, 288, 11)), rssi(getbits32(words, 12, 299, 11)),
                       rssi(getbits32(words, 12, 320, 11)),
                       snr_lte(getbits32(snr_words, 2, 0, 9)), snr_lte(getbits32(snr_words, 2, 9, 9)),
                       snr_lte(getbits32(snr_words, 2, 32, 9)), snr_lte(getbits32(snr_words, 2, 42, 8)),
                       (double)prj_sir / 16.0, (double)post_ic_rsrq * 0.0625 - 30.0,
                       cinr0, cinr1, cinr2, cinr3);
                parsed = 1;
            }
        } else if (sub_ver == 59 && sub_len >= 16) {
            uint32_t earfcn = get32(sub);
            uint32_t num_cells = get32(sub + 4);
            uint32_t valid_rx = get32(sub + 8);
            uint32_t rx_map = get32(sub + 12);
            size_t cell_pos = 16, cell_size = 156;
            for (uint32_t idx = 0; idx < num_cells && cell_pos + 32 <= sub_len; idx++, cell_pos += cell_size) {
                size_t have = sub_len - cell_pos;
                if (have > cell_size) have = cell_size;
                uint16_t meta0 = get16(sub + cell_pos);
                uint16_t meta1 = get16(sub + cell_pos + 2);
                uint16_t meta2 = get16(sub + cell_pos + 4);
                json_prefix(log_id, qts, "LTE", "per_antenna_measurement_raw");
                printf(",\"version\":%u,\"earfcn\":%u,\"cell_index\":%u,\"valid_rx\":%u,"
                       "\"rx_map\":%u,\"pci_guess\":%u,\"serving_cell_index_guess\":%u,"
                       "\"is_serving_cell_guess\":%s,\"meta_words\":[%u,%u,%u],\"cell_hex\":\"",
                       sub_ver, earfcn, idx, valid_rx, rx_map, meta0 & 0x1ff,
                       (meta0 >> 9) & 7, (meta0 & 0x1000) ? "true" : "false",
                       meta0, meta1, meta2);
                hexprint(stdout, sub + cell_pos, have);
                printf("\"}\n");
                parsed = 1;
            }
        } else if (opt_debug) {
            json_prefix(log_id, qts, "LTE", "per_antenna_measurement_unknown");
            printf(",\"subpacket_id\":%u,\"subpacket_version\":%u,\"subpacket_size\":%u,\"payload_hex\":\"",
                   sub_id, sub_ver, sub_size);
            hexprint(stdout, sub, sub_len < 192 ? sub_len : 192);
            printf("\"}\n");
            parsed = 1;
        }
    }
    return parsed;
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
    if (rel_maj == 0 && rel_min == 4) off = 4;
    else if (rel_maj == 3 && rel_min == 0) off = 4;
    else if (rel_maj == 3 && (rel_min == 2 || rel_min == 3)) off = 7;
    else return 0;
    uint16_t pci = get16(b + off);
    uint32_t dl = rel_maj == 0 ? get32(b + off + 2) : get32(b + off + 10);
    uint32_t ul = rel_maj == 0 ? get32(b + off + 6) : get32(b + off + 14);
    uint16_t band = rel_maj == 0 ? get16(b + off + 32) : get16(b + off + 40);
    json_prefix(log_id, qts, "NR", "serving_cell_info");
    printf(",\"version\":\"%u.%u\",\"pci\":%u,\"dl_nr_arfcn\":%u,\"ul_nr_arfcn\":%u,\"band\":%u}\n",
           rel_maj, rel_min, pci, dl, ul, band);
    return 1;
}

static int parse_lte_mac_v1_dl_subpacket(uint16_t log_id, uint64_t qts, uint8_t sub_ver, const uint8_t *sub, size_t sub_len) {
    if (sub_len < 1) return 0;
    size_t pos = 1;
    int parsed = 0;
    for (uint8_t i = 0; i < sub[0]; i++) {
        uint16_t sfn_subfn, pmch_id, dl_tbs, padding;
        uint8_t rnti_type, harq_id, rlc_pdus, header_len, cell_id = 0;
        if (sub_ver == 2) {
            if (pos + 12 > sub_len) break;
            sfn_subfn = get16(sub + pos);
            rnti_type = sub[pos + 2];
            harq_id = sub[pos + 3];
            pmch_id = get16(sub + pos + 4);
            dl_tbs = get16(sub + pos + 6);
            rlc_pdus = sub[pos + 8];
            padding = get16(sub + pos + 9);
            header_len = sub[pos + 11];
            pos += 12;
        } else if (sub_ver == 4) {
            if (pos + 14 > sub_len) break;
            cell_id = sub[pos + 1];
            sfn_subfn = get16(sub + pos + 2);
            rnti_type = sub[pos + 4];
            harq_id = sub[pos + 5];
            pmch_id = get16(sub + pos + 6);
            dl_tbs = get16(sub + pos + 8);
            rlc_pdus = sub[pos + 10];
            padding = get16(sub + pos + 11);
            header_len = sub[pos + 13];
            pos += 14;
        } else {
            return parsed;
        }
        if (pos + header_len > sub_len) header_len = (uint8_t)(sub_len - pos);
        json_prefix(log_id, qts, "LTE", "lte_mac_transport_block");
        printf(",\"version\":%u,\"direction\":\"DL\",\"sample_index\":%u,"
               "\"sfn\":%u,\"subframe\":%u,\"rnti_type\":%u,\"harq_id\":%u,"
               "\"pmch_id\":%u,\"tbs_bytes\":%u,\"rlc_pdus\":%u,"
               "\"padding_bytes\":%u,\"mac_header_len\":%u",
               sub_ver, i, sfn_subfn >> 4, sfn_subfn & 0xf, rnti_type,
               harq_id, pmch_id, dl_tbs, rlc_pdus, padding, header_len);
        if (sub_ver == 4) printf(",\"cell_id\":%u,\"cc_id\":%u", cell_id, cell_id);
        printf(",\"mac_header_hex\":\"");
        hexprint(stdout, sub + pos, header_len);
        printf("\"}\n");
        pos += header_len;
        parsed = 1;
    }
    return parsed;
}

static int parse_lte_mac_v1_ul_subpacket(uint16_t log_id, uint64_t qts, uint8_t sub_ver, const uint8_t *sub, size_t sub_len) {
    if (sub_len < 1) return 0;
    size_t pos = 1;
    int parsed = 0;
    for (uint8_t i = 0; i < sub[0]; i++) {
        uint16_t sfn_subfn, grant, padding;
        uint8_t subid = 0, cell_id = 0, harq_id, rnti_type, rlc_pdus, bsr_event, bsr_trig, header_len;
        if (sub_ver == 1) {
            if (pos + 12 > sub_len) break;
            harq_id = sub[pos];
            rnti_type = sub[pos + 1];
            sfn_subfn = get16(sub + pos + 2);
            grant = get16(sub + pos + 4);
            rlc_pdus = sub[pos + 6];
            padding = get16(sub + pos + 7);
            bsr_event = sub[pos + 9];
            bsr_trig = sub[pos + 10];
            header_len = sub[pos + 11];
            pos += 12;
        } else if (sub_ver == 2 || sub_ver == 3 || sub_ver == 5 || sub_ver == 8) {
            if (pos + 14 > sub_len) break;
            subid = sub[pos];
            cell_id = sub[pos + 1];
            harq_id = sub[pos + 2];
            rnti_type = sub[pos + 3];
            sfn_subfn = get16(sub + pos + 4);
            grant = get16(sub + pos + 6);
            rlc_pdus = sub[pos + 8];
            padding = get16(sub + pos + 9);
            bsr_event = sub[pos + 11];
            bsr_trig = sub[pos + 12];
            header_len = sub[pos + 13];
            pos += 14;
        } else {
            return parsed;
        }
        if (pos + header_len > sub_len) header_len = (uint8_t)(sub_len - pos);
        json_prefix(log_id, qts, "LTE", "lte_mac_transport_block");
        printf(",\"version\":%u,\"direction\":\"UL\",\"sample_index\":%u,"
               "\"sfn\":%u,\"subframe\":%u,\"rnti_type\":%u,\"harq_id\":%u,"
               "\"grant\":%u,\"rlc_pdus\":%u,\"padding_bytes\":%u,"
               "\"bsr_event\":%u,\"bsr_trigger\":%u,\"mac_header_len\":%u",
               sub_ver, i, sfn_subfn >> 4, sfn_subfn & 0xf, rnti_type,
               harq_id, grant, rlc_pdus, padding, bsr_event, bsr_trig, header_len);
        if (sub_ver != 1) printf(",\"subid\":%u,\"cell_id\":%u", subid, cell_id);
        printf(",\"mac_header_hex\":\"");
        hexprint(stdout, sub + pos, header_len);
        printf("\"}\n");
        pos += header_len;
        parsed = 1;
    }
    return parsed;
}

static int parse_lte_mac_v1(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len, int downlink) {
    if (len < 8) return 0;
    size_t pos = 4;
    int parsed = 0;
    for (uint8_t i = 0; i < b[1] && pos + 4 <= len; i++) {
        uint8_t sub_id = b[pos], sub_ver = b[pos + 1];
        uint16_t sub_size = get16(b + pos + 2);
        size_t sub_start = pos + 4, sub_end;
        if (sub_size >= 4 && pos + sub_size <= len) sub_end = pos + sub_size;
        else sub_end = pos + 4 + sub_size <= len ? pos + 4 + sub_size : len;
        if (sub_start > sub_end) break;
        const uint8_t *sub = b + sub_start;
        size_t sub_len = sub_end - sub_start;
        if (downlink && sub_id == 0x07) parsed |= parse_lte_mac_v1_dl_subpacket(log_id, qts, sub_ver, sub, sub_len);
        else if (!downlink && sub_id == 0x08) parsed |= parse_lte_mac_v1_ul_subpacket(log_id, qts, sub_ver, sub, sub_len);
        pos = sub_end;
    }
    return parsed;
}

static int parse_lte_mac_dl_v49(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 8) return 0;
    uint8_t ver = b[0];
    uint16_t num_tb = get16(b + 4);
    uint8_t num_lcid = b[6], reason = b[7];
    size_t pos = 8;
    if (ver == 0x31) pos += 19 * 28;
    int parsed = 0;
    for (uint16_t i = 0; i < num_tb && pos + 16 <= len; i++) {
        uint32_t size = get32(b + pos);
        uint32_t pad = get32(b + pos + 4);
        uint32_t sfn_word = get32(b + pos + 8);
        uint8_t cc_harq = b[pos + 12];
        uint8_t num_sdu = b[pos + 13];
        uint16_t hdr_len_word = get16(b + pos + 14);
        pos += 16;
        json_prefix(log_id, qts, "LTE", "lte_mac_transport_block");
        printf(",\"version\":%u,\"direction\":\"DL\",\"tb_index\":%u,"
               "\"reason\":%u,\"num_lcid\":%u,\"sfn\":%u,\"subframe\":%u,"
               "\"rnti_type\":%u,\"cc_id\":%u,\"harq_id\":%u,"
               "\"tbs_bytes\":%u,\"padding_bytes\":%u,\"num_sdu\":%u,"
               "\"mac_header_len\":%u}\n",
               ver, i, reason, num_lcid, bitu32(sfn_word, 0, 10),
               bitu32(sfn_word, 10, 4), bitu32(sfn_word, 15, 4),
               bitu32(cc_harq, 0, 4), bitu32(cc_harq, 4, 4), size, pad,
               num_sdu, bitu32(hdr_len_word, 0, 12));
        for (uint8_t j = 0; j < num_sdu && pos + 12 <= len; j++) {
            uint32_t common = (uint32_t)b[pos] | ((uint32_t)b[pos + 1] << 8) | ((uint32_t)b[pos + 2] << 16);
            uint8_t is_mce = bitu32(common, 0, 1);
            pos += 3;
            const uint8_t *info = b + pos;
            pos += 9;
            if (!is_mce && pos <= len) {
                uint8_t num_pdcp_grp = info[5];
                uint16_t num_dyn = get16(info + 6);
                pos += (size_t)num_dyn * 4;
                for (uint8_t g = 0; g < num_pdcp_grp; g++) {
                    uint8_t has_more = 1;
                    while (has_more && pos + 4 <= len) {
                        uint32_t grp = get32(b + pos);
                        has_more = bitu32(grp, 0, 1);
                        pos += 4;
                    }
                }
            }
        }
        parsed = 1;
    }
    return parsed;
}

static int parse_lte_mac(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len, int downlink) {
    if (!len) return 0;
    if (downlink && (b[0] == 0x31 || b[0] == 0x32)) return parse_lte_mac_dl_v49(log_id, qts, b, len);
    if (b[0] == 1) return parse_lte_mac_v1(log_id, qts, b, len, downlink);
    return 0;
}

static int parse_lte_phy_pusch_tx_candidate(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 108 || b[0] != 0xA1 || ((len - 8) % 100) != 0) return 0;
    uint16_t header_tti = get16(b + 4);
    size_t count = (len - 8) / 100;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *rec = b + 8 + i * 100;
        uint16_t tti = get16(rec);
        uint16_t grant = get16(rec + 8);
        int16_t tx_power_raw = (int16_t)get16(rec + 4);
        json_prefix(log_id, qts, "LTE", "lte_phy_pusch_tx_candidate");
        printf(",\"version\":%u,\"subversion\":%u,\"header_tti\":%u,"
               "\"record_index\":%zu,\"record_count\":%zu,\"tti\":%u,"
               "\"sfn_guess\":%u,\"subframe_guess\":%u,\"record_flags_raw\":%u,"
               "\"grant\":%u,\"tx_power_raw\":%d,\"field_10_raw\":%u,"
               "\"field_34_raw\":%u,\"field_36_raw\":%u,\"field_40_raw\":%u,"
               "\"field_42_raw\":%u,\"field_46_raw\":%u,\"record_hex\":\"",
               b[0], b[1], header_tti, i, count, tti, (tti / 10) % 1024,
               tti % 10, get16(rec + 2), grant, tx_power_raw, get16(rec + 10),
               get16(rec + 34), get16(rec + 36), get16(rec + 40), get16(rec + 42),
               get16(rec + 46));
        hexprint(stdout, rec, 100);
        printf("\"}\n");
    }
    return 1;
}

static void parse_log_body(uint16_t log_id, uint64_t qts, const uint8_t *body, size_t body_len) {
    int parsed = 0;
    if (log_id == LOG_LTE_SMEAS) parsed = parse_lte_smeas(log_id, qts, body, body_len);
    else if (log_id == LOG_LTE_SMEAS_RESP) parsed = parse_lte_smeas_resp(log_id, qts, body, body_len);
    else if (log_id == LOG_LTE_RRC_SCELL) parsed = parse_lte_rrc_scell(log_id, qts, body, body_len);
    else if (log_id == LOG_NR_RRC_SCELL) parsed = parse_nr_rrc_scell(log_id, qts, body, body_len);
    else if (log_id == LOG_LTE_MAC_DL) parsed = parse_lte_mac(log_id, qts, body, body_len, 1);
    else if (log_id == LOG_LTE_MAC_UL) parsed = parse_lte_mac(log_id, qts, body, body_len, 0);
    else if (log_id == LOG_LTE_PHY_PUSCH_TX_REPORT) parsed = parse_lte_phy_pusch_tx_candidate(log_id, qts, body, body_len);
    else if (log_id == LOG_LTE_CA) {
        got_lte = 1;
        write_combo(combo_dir, "b0cd_qlte.hex", body, body_len);
        json_prefix(log_id, qts, "LTE", "supported_ca_combos_raw");
        printf(",\"format\":\"QLTE\",\"payload_hex\":\""); hexprint(stdout, body, body_len); printf("\"}\n");
        parsed = 1;
    } else if (log_id == LOG_NR_CA) {
        got_nr = 1;
        write_combo(combo_dir, "b826_qnr.hex", body, body_len);
        json_prefix(log_id, qts, "NR", "supported_ca_combos_raw");
        printf(",\"format\":\"QNR\",\"payload_hex\":\""); hexprint(stdout, body, body_len); printf("\"}\n");
        parsed = 1;
    }
    if ((!parsed && opt_debug) || opt_raw ||
        (opt_probe_scheduling && is_probe_scheduling_log(log_id)) ||
        (opt_probe_phy && is_probe_phy_log(log_id))) {
        json_prefix(log_id, qts, "RAW", "diag_log_raw");
        printf(",\"body_len\":%zu,\"body_hex\":\"", body_len);
        hexprint(stdout, body, body_len);
        printf("\"}\n");
    }
    fflush(stdout);
}

static void on_log(unsigned char *ptr, int len) {
    if (!ptr || len < 12) return;
    uint16_t rec_len = get16(ptr);
    uint16_t log_id = get16(ptr + 2);
    uint64_t qts = get64(ptr + 4);
    if (rec_len > (uint16_t)len) rec_len = (uint16_t)len;
    if (rec_len < 12) return;
    parse_log_body(log_id, qts, ptr + 12, rec_len - 12);
}

static void on_event(unsigned char *ptr, int len) {
    if (!opt_debug) return;
    printf("{\"event\":\"diag_event_raw\",\"len\":%d,\"payload_hex\":\"", len);
    hexprint(stdout, ptr, len > 256 ? 256 : (size_t)len);
    printf("\"}\n");
    fflush(stdout);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--seconds N] [--proc msm|mdm|auto] [--debug] [--raw]\n"
        "          [--mac] [--probe-scheduling] [--probe-phy] [--combo-dir DIR]\n"
        "          [--wait-uecap lte|nr|both]\n", argv0);
}

int main(int argc, char **argv) {
    int seconds = 0, mac = 0, proc = DIAG_PROC_MSM;
    const char *wait = NULL;
    static struct option opts[] = {
        {"seconds", required_argument, 0, 's'},
        {"proc", required_argument, 0, 'p'},
        {"debug", no_argument, 0, 1},
        {"raw", no_argument, 0, 2},
        {"mac", no_argument, 0, 3},
        {"combo-dir", required_argument, 0, 4},
        {"wait-uecap", required_argument, 0, 5},
        {"probe-scheduling", no_argument, 0, 6},
        {"probe-phy", no_argument, 0, 7},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "s:p:h", opts, NULL)) != -1) {
        if (c == 's') seconds = atoi(optarg);
        else if (c == 'p') {
            if (!strcmp(optarg, "mdm")) proc = DIAG_PROC_MDM;
            else proc = DIAG_PROC_MSM;
        } else if (c == 1) opt_debug = 1;
        else if (c == 2) opt_raw = 1;
        else if (c == 3) mac = 1;
        else if (c == 4) combo_dir = optarg;
        else if (c == 5) wait = optarg;
        else if (c == 6) opt_probe_scheduling = 1;
        else if (c == 7) opt_probe_phy = 1;
        else { usage(argv[0]); return c == 'h' ? 0 : 2; }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    int dci_sig = SIGRTMIN + 15;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_dci_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(dci_sig, &sa, NULL);
    diag_disable_console = 1;

    if (!Diag_LSM_Init(NULL)) {
        fprintf(stderr, "Diag_LSM_Init failed: errno=%d\n", errno);
        return 1;
    }

    int client_id = -1;
    diag_dci_peripherals list = DIAG_CON_MPSS;
    int err = diag_register_dci_client(&client_id, &list, proc, &dci_sig);
    if (err != DIAG_DCI_NO_ERROR) {
        fprintf(stderr, "diag_register_dci_client proc=%d failed err=%d errno=%d\n", proc, err, errno);
        Diag_LSM_DeInit();
        return 1;
    }
    err = diag_get_dci_support_list_proc(proc, &list);
    if (opt_debug && err == DIAG_DCI_NO_ERROR) {
        printf("{\"event\":\"dci_support\",\"proc\":%d,\"mpss\":%s,\"apss\":%s,\"lpass\":%s,\"wcnss\":%s}\n",
               proc, (list & DIAG_CON_MPSS) ? "true" : "false",
               (list & DIAG_CON_APSS) ? "true" : "false",
               (list & DIAG_CON_LPASS) ? "true" : "false",
               (list & DIAG_CON_WCNSS) ? "true" : "false");
    }
    err = diag_register_dci_signal_data(client_id, dci_sig);
    if (err != DIAG_DCI_NO_ERROR && err != DIAG_DCI_NOT_SUPPORTED) {
        fprintf(stderr, "diag_register_dci_signal_data failed err=%d errno=%d\n", err, errno);
    }
    err = diag_register_dci_stream_proc(client_id, on_log, on_event);
    if (err != DIAG_DCI_NO_ERROR) {
        fprintf(stderr, "diag_register_dci_stream_proc failed err=%d errno=%d\n", err, errno);
    }
    diag_dci_vote_real_time(client_id, MODE_REALTIME);

    uint16 logs[64];
    int count = 0;
    logs[count++] = LOG_LTE_SMEAS;
    logs[count++] = LOG_LTE_NMEAS;
    logs[count++] = LOG_LTE_SMEAS_RESP;
    logs[count++] = LOG_LTE_CELL_INFO;
    logs[count++] = LOG_LTE_RRC_OTA;
    logs[count++] = LOG_LTE_RRC_MIB;
    logs[count++] = LOG_LTE_RRC_SCELL;
    logs[count++] = LOG_LTE_CA;
    logs[count++] = LOG_NR_NAS_5GMM_STATE;
    logs[count++] = LOG_NR_RRC_OTA;
    logs[count++] = LOG_NR_MIB;
    logs[count++] = LOG_NR_RRC_SCELL;
    logs[count++] = LOG_NR_RRC_CFG;
    logs[count++] = LOG_NR_CA;
    logs[count++] = LOG_NR_ML1;
    if (mac) {
        logs[count++] = LOG_LTE_MAC_DL;
        logs[count++] = LOG_LTE_MAC_UL;
    }
    if (opt_probe_scheduling) {
        logs[count++] = LOG_LTE_ML1_MAC_RAR_MSG1;
        logs[count++] = LOG_LTE_ML1_MAC_RAR_MSG2;
        logs[count++] = LOG_LTE_ML1_MAC_RAR_MSG3;
        logs[count++] = LOG_LTE_ML1_MAC_RAR_MSG4;
        logs[count++] = LOG_LTE_ML1_CONNECTED_INTRA_FREQ;
        logs[count++] = LOG_LTE_ML1_NEIGHBOR_REQ_RESP;
        logs[count++] = LOG_LTE_ML1_SEARCH_REQ_RESP;
        logs[count++] = LOG_LTE_ML1_CONNECTED_NEIGHBOR_REQ_RESP;
        logs[count++] = LOG_NR_MAC_RACH_ATTEMPT;
    }
    if (opt_probe_phy) {
        logs[count++] = LOG_LTE_PHY_PDSCH_DEMAPPER_CONFIG;
        logs[count++] = LOG_LTE_PHY_PDCCH_DECODING_RESULT;
        logs[count++] = LOG_LTE_PHY_PDCCH_DECODING_RESULT2;
        logs[count++] = LOG_LTE_PHY_PUSCH_TX_REPORT;
        logs[count++] = LOG_LTE_PHY_PUCCH_TX_REPORT;
        logs[count++] = LOG_LTE_PHY_PUSCH_CSF_REPORT;
        logs[count++] = LOG_LTE_PHY_PDSCH_STAT_INDICATION;
        logs[count++] = LOG_LTE_PHY_PDCCH_PHICH_INDICATION;
        logs[count++] = LOG_LTE_PHY_GM_TX_REPORT;
        logs[count++] = LOG_LTE_PHY_PDSCH_STAT_INDICATION2;
        logs[count++] = LOG_LTE_PHY_PUSCH_STAT_INDICATION;
        logs[count++] = LOG_LTE_PHY_PUCCH_CSF_REPORT;
        logs[count++] = LOG_LTE_PHY_CQI_REPORT;
        logs[count++] = LOG_LTE_PHY_RI_REPORT;
        logs[count++] = LOG_LTE_PHY_PMI_REPORT;
    }
    err = diag_log_stream_config(client_id, ENABLE, logs, count);
    if (err != DIAG_DCI_NO_ERROR) {
        fprintf(stderr, "diag_log_stream_config enable failed err=%d errno=%d\n", err, errno);
    }

    time_t start = time(NULL);
    while (running) {
        if (seconds > 0 && time(NULL) - start >= seconds) break;
        if (wait) {
            if ((!strcmp(wait, "lte") && got_lte) || (!strcmp(wait, "nr") && got_nr) ||
                (!strcmp(wait, "both") && got_lte && got_nr)) break;
        }
        sleep(1);
    }

    diag_log_stream_config(client_id, DISABLE, logs, count);
    diag_disable_all_logs(client_id);
    diag_deregister_dci_signal_data(client_id, dci_sig);
    diag_release_dci_client(&client_id);
    Diag_LSM_DeInit();
    return 0;
}
