#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
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
#define LOG_LTE_PHY_SRS_TX_REPORT 0xB140
#define LOG_LTE_PHY_RACH_TX_REPORT 0xB144
#define LOG_LTE_PHY_PUCCH_CSF_REPORT 0xB14D
#define LOG_LTE_PHY_PUSCH_CSF_REPORT 0xB14E
#define LOG_LTE_PHY_PDCCH_PHICH_INDICATION 0xB16B
#define LOG_LTE_PHY_GM_TX_REPORT 0xB16D
#define LOG_LTE_PHY_PDSCH_STAT_INDICATION2 0xB173
#define LOG_LTE_PHY_PUSCH_STAT_INDICATION 0xB174
#define LOG_LTE_PHY_LEGACY_B175 0xB175
#define LOG_LTE_PHY_LEGACY_B176 0xB176
#define LOG_LTE_PHY_LEGACY_B177 0xB177
#define LOG_LTE_PHY_LEGACY_B178 0xB178
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
#define LOG_NR_MAC_UL_TB_STATS 0xB881
#define LOG_NR_MAC_UL_PHYS_CH_SCHED 0xB883
#define LOG_NR_MAC_PDSCH_STATS 0xB888
#define LOG_NR_MAC_RACH_ATTEMPT 0xB88A
#define LOG_NR_ML1_SERVING_CELL_BEAM_MGMT 0xB975
#define LOG_NR_ML1 0xB97F

static volatile sig_atomic_t running = 1;
static int opt_debug = 0;
static int opt_raw = 0;
static int opt_no_raw_log = 0;
static int opt_stream_json = 1;
static int opt_gui_lite = 0;
static int opt_full_logset = 0;
static int opt_probe_scheduling = 0;
static int opt_probe_phy = 0;
static const char *combo_dir = NULL;
static const char *snapshot_file = NULL;
static int snapshot_interval_ms = 10000;
static int sample_window_ms = -1;
static int stale_after_ms = 30000;
static int nice_increment = 0;
static int log_stream_enabled = 0;
static int opt_oneshot = 0;
static int sample_min_ms = 800;
static int require_mode = 1;
static int got_lte = 0;
static int got_nr = 0;
static time_t runtime_start = 0;
static unsigned long long events_seen = 0;
static unsigned long long sample_events_seen = 0;
static uint64_t sample_started_ms = 0;
static int sample_signal_seen = 0;
static int sample_mac_seen = 0;
static int sample_nr_seen = 0;
static char last_error[160] = "";

#define REQUIRE_ANY 0
#define REQUIRE_SIGNAL 1
#define REQUIRE_SIGNAL_MAC 2
#define REQUIRE_MAC 3
#define REQUIRE_NR 4
#define MAX_LTE_ANTENNA_CELLS 8
#define MAX_LTE_CC 8
#define MAX_LOG_COUNTS 64
#define MAX_NR_LAYERS 4
#define MAX_NR_CELLS 4
#define MAX_NR_BEAMS 4

typedef struct {
    const char *name;
    const char *source_candidates;
} missing_metric_t;

typedef struct {
    int valid;
    uint8_t version;
    uint8_t rrc_release;
    uint32_t earfcn;
    uint16_t pci;
    double rsrp_dbm;
    double avg_rsrp_dbm;
    double rssi_dbm;
    double rsrq_db;
    double avg_rsrq_db;
    uint64_t qts;
    time_t updated_at;
} lte_serving_measurement_t;

typedef struct {
    int valid;
    uint8_t version;
    uint32_t earfcn;
    uint32_t cell_index;
    uint32_t valid_rx;
    uint32_t rx_map;
    uint16_t pci;
    uint16_t serving_cell_index;
    int is_serving_cell;
    uint16_t sfn;
    uint8_t subframe;
    double rsrp_dbm[4];
    double combined_rsrp_dbm;
    double filtered_rsrp_dbm;
    double rsrq_db[4];
    double combined_rsrq_db;
    double filtered_rsrq_db;
    double rssi_dbm[4];
    double combined_rssi_dbm;
    double snr_db[4];
    double projected_sir_db;
    double post_ic_rsrq_db;
    int32_t cinr_raw[4];
    uint64_t qts;
    time_t updated_at;
} lte_per_antenna_t;

typedef struct {
    int valid;
    uint8_t version;
    uint16_t pci;
    uint32_t dl_earfcn;
    uint32_t ul_earfcn;
    uint32_t band;
    uint16_t dl_bandwidth_prb;
    uint16_t ul_bandwidth_prb;
    uint32_t cell_id;
    uint16_t tac;
    uint16_t mcc;
    char mnc[8];
    uint64_t qts;
    time_t updated_at;
} lte_serving_info_t;

typedef struct {
    int valid;
    uint8_t version;
    uint8_t cc_id;
    uint16_t sfn;
    uint8_t subframe;
    uint8_t rnti_type;
    uint8_t harq_id;
    uint32_t tbs_bytes;
    uint32_t padding_bytes;
    uint8_t num_sdu;
    uint8_t num_lcid;
    uint64_t qts;
    time_t updated_at;
} lte_mac_dl_t;

typedef struct {
    int valid;
    uint8_t version;
    uint8_t cc_id;
    uint16_t sfn;
    uint8_t subframe;
    uint8_t rnti_type;
    uint8_t harq_id;
    uint16_t grant;
    uint8_t rlc_pdus;
    uint16_t padding_bytes;
    uint8_t bsr_event;
    uint8_t bsr_trigger;
    uint64_t qts;
    time_t updated_at;
} lte_mac_ul_t;

typedef struct {
    int valid;
    uint8_t version;
    uint16_t serving_cell_id;
    uint16_t header_word;
    uint16_t dispatch_sfn;
    uint8_t dispatch_subframe;
    uint16_t record_index;
    uint16_t record_count;
    uint16_t sfn;
    uint8_t subframe;
    uint16_t tti;
    uint8_t carrier_id;
    uint8_t ack_flag;
    uint8_t cqi_flag;
    uint8_t ri_flag;
    uint8_t frequency_hopping;
    uint8_t retx_index;
    uint8_t rv;
    uint8_t mirror_hopping;
    uint8_t ra_type;
    uint16_t rb_start_slot0;
    uint16_t rb_start_slot1;
    uint16_t rb_count;
    uint16_t tb_size;
    uint16_t grant;
    uint16_t coding_rate_raw;
    double coding_rate;
    uint8_t pusch_mod_order;
    uint32_t mod_gain_cluster_raw;
    uint32_t tx_cqi_raw;
    uint8_t tx_power_raw;
    int16_t tx_power_dbm_candidate;
    uint64_t qts;
    time_t updated_at;
} lte_phy_pusch_t;

typedef struct {
    int valid;
    uint8_t tb_index;
    uint8_t harq_id;
    uint8_t rv;
    uint8_t ndi;
    uint8_t crc_result;
    uint8_t rnti_type;
    uint8_t discarded_retx_present;
    uint8_t did_recombining;
    uint16_t tb_size;
    uint8_t mcs;
    uint8_t num_rbs;
    uint8_t modulation_type;
    uint8_t qed2_interim;
} lte_phy_pdsch_tb_t;

typedef struct {
    int valid;
    int decoded;
    uint8_t version;
    uint8_t subversion;
    uint16_t header_word;
    uint16_t record_index;
    uint16_t record_count;
    uint16_t sfn;
    uint8_t subframe;
    uint8_t num_rbs;
    uint8_t num_layers;
    uint8_t num_transport_blocks;
    uint8_t serving_cell_id;
    uint8_t hsic_enabled;
    lte_phy_pdsch_tb_t tb[2];
    unsigned long long crc_pass_total;
    unsigned long long crc_fail_total;
    double dl_bler;
    uint8_t mcs_candidate_raw;
    uint16_t field_0_raw;
    uint16_t field_2_raw;
    uint16_t field_4_raw;
    uint16_t field_8_raw;
    uint16_t field_12_raw;
    uint16_t field_16_raw;
    uint16_t field_18_raw;
    uint16_t field_20_raw;
    uint16_t field_24_raw;
    uint16_t field_28_raw;
    uint64_t qts;
    time_t updated_at;
} lte_phy_pdsch_t;

typedef struct {
    int valid;
    char version[16];
    uint16_t pci;
    uint32_t dl_nr_arfcn;
    uint32_t ul_nr_arfcn;
    uint16_t band;
    uint64_t qts;
    time_t updated_at;
} nr_serving_info_t;

typedef struct {
    int valid;
    uint16_t index;
    uint16_t ssb_index;
    int rx_beam[2];
    uint64_t ssb_ref_timing;
    double rsrp_dbm[2];
    int has_rsrq;
    double rsrq_db[2];
    double filtered_nr2nr_rsrp_dbm;
    double filtered_nr2nr_rsrq_db;
    double filtered_l2nr_rsrp_dbm;
    double filtered_l2nr_rsrq_db;
} nr_beam_t;

typedef struct {
    int valid;
    uint16_t index;
    uint16_t pci;
    uint16_t pbch_sfn;
    uint8_t num_beams;
    double rsrp_dbm;
    double rsrq_db;
    nr_beam_t beams[MAX_NR_BEAMS];
} nr_cell_t;

typedef struct {
    int valid;
    uint8_t layer;
    uint32_t nr_arfcn;
    int has_cc_id;
    uint8_t cc_id;
    uint8_t num_cells;
    uint8_t serving_cell_index;
    uint16_t serving_pci;
    uint8_t serving_ssb;
    uint8_t serving_rsrp_count;
    double serving_rsrp_dbm[4];
    int rx_beam[2];
    uint16_t rfic_id;
    uint16_t subarray[2];
    uint8_t cells_count;
    nr_cell_t cells[MAX_NR_CELLS];
    uint64_t qts;
    time_t updated_at;
} nr_layer_t;

typedef struct {
    int valid;
    uint16_t major;
    uint16_t minor;
    uint16_t record_index;
    uint16_t record_count;
    uint64_t tb_new_tx_bytes;
    uint64_t tb_retx_bytes;
    uint64_t num_mcs;
    uint64_t num_prb;
    uint64_t phr;
    uint64_t total_power;
    uint32_t num_new_tx_tb;
    uint32_t num_retx_tb;
    uint32_t num_dtx;
    uint32_t num_ri;
    uint32_t ri;
    uint32_t num_cqi;
    uint32_t cqi;
    uint32_t num_phr;
    uint32_t tpc_accum;
    uint32_t num_ulsch_sched;
    uint32_t num_no_ulsch_sched;
    uint16_t pcmax_raw;
    uint16_t flush_gap_count;
    double avg_mcs_candidate;
    double avg_prb_candidate;
    double avg_phr_candidate;
    double avg_ri_candidate;
    double avg_cqi_candidate;
    double retx_tb_ratio;
    double ulsch_sched_ratio;
    double pcmax_dbm_candidate;
    uint64_t qts;
    time_t updated_at;
} nr_mac_ul_tb_stats_t;

typedef struct {
    int valid;
    uint16_t major;
    uint16_t minor;
    uint16_t record_index;
    uint16_t record_count;
    uint32_t carrier_id;
    uint32_t num_slots_elapsed;
    uint32_t num_pdsch_decode;
    uint32_t num_crc_pass_tb;
    uint32_t num_crc_fail_tb;
    uint32_t num_retx;
    uint32_t ack_as_nack;
    uint32_t harq_failure;
    uint64_t crc_pass_tb_bytes;
    uint64_t crc_fail_tb_bytes;
    uint64_t tb_bytes;
    uint64_t padding_bytes;
    uint64_t retx_bytes;
    double dl_bler;
    double byte_error_ratio;
    double retx_ratio;
    uint64_t qts;
    time_t updated_at;
} nr_mac_pdsch_stats_t;

typedef struct {
    int valid;
    char format[8];
    char path[512];
    size_t bytes;
    uint64_t qts;
    time_t updated_at;
} combo_state_t;

typedef struct {
    int valid;
    uint16_t log_id;
    unsigned long long count;
    uint64_t last_qts;
    time_t last_seen_at;
} log_count_t;

typedef struct {
    lte_serving_measurement_t lte_serving;
    lte_serving_info_t lte_info;
    lte_per_antenna_t lte_ant[MAX_LTE_ANTENNA_CELLS];
    lte_mac_dl_t mac_dl[MAX_LTE_CC];
    lte_mac_ul_t mac_ul[MAX_LTE_CC];
    lte_phy_pusch_t pusch;
    lte_phy_pdsch_t pdsch;
    nr_serving_info_t nr_serving;
    char nr_ml1_version[16];
    uint8_t nr_layer_count;
    uint8_t nr_ssb_periodicity;
    uint64_t nr_ml1_qts;
    time_t nr_ml1_updated_at;
    nr_layer_t nr_layers[MAX_NR_LAYERS];
    nr_mac_ul_tb_stats_t nr_ul_tb_stats;
    nr_mac_pdsch_stats_t nr_pdsch_stats[MAX_LTE_CC];
    combo_state_t lte_combo;
    combo_state_t nr_combo;
} snapshot_state_t;

static snapshot_state_t state;
static log_count_t log_counts[MAX_LOG_COUNTS];
static unsigned long long lte_pdsch_crc_pass_total = 0;
static unsigned long long lte_pdsch_crc_fail_total = 0;

static const missing_metric_t lte_missing_metrics[] = {
    {"dl_mcs", "LTE 0xB173 v36"},
    {"ul_mcs", "LTE 0xB139/0xB174"},
    {"dl_modulation", "LTE 0xB173 v36"},
    {"ul_modulation", "LTE 0xB139/0xB174"},
    {"dl_rb_alloc", "LTE 0xB173 v36"},
    {"ul_rb_alloc", "LTE 0xB139/0xB174"},
    {"cqi", "LTE 0xB14D/0xB14E"},
    {"ri", "LTE 0xB14D/0xB14E"},
    {"pmi", "LTE 0xB14D/0xB14E"},
    {"bler", "LTE 0xB173 v36"},
    {"tx_power_dbm", "LTE 0xB139/0xB16D/0xB174"}
};

static const missing_metric_t nr_missing_metrics[] = {
    {"nr_mode", "NR NAS/RRC state logs"},
    {"endc_anchor", "LTE/NR RRC configuration logs"},
    {"ss_rsrp_dbm", "NR ML1 measurement database"},
    {"ss_rsrq_db", "NR ML1 measurement database"},
    {"ss_sinr_db", "NR ML1 measurement/log version mapping"},
    {"per_rx_rsrp_dbm", "NR ML1 measurement database"},
    {"per_rx_sinr_db", "NR ML1 measurement/log version mapping"},
    {"dl_mcs", "NR MAC/PHY scheduling logs"},
    {"ul_mcs", "NR MAC/PHY scheduling logs"},
    {"dl_modulation", "NR MAC/PHY scheduling logs"},
    {"ul_modulation", "NR MAC/PHY scheduling logs"},
    {"dl_rb_alloc", "NR MAC/PHY scheduling logs"},
    {"ul_rb_alloc", "NR MAC/PHY scheduling logs"},
    {"rank_ri", "NR MAC/PHY scheduling logs"},
    {"cqi", "NR MAC/PHY scheduling logs"},
    {"tx_power_dbm", "NR MAC/PHY scheduling logs"}
};

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

static double q7_signed(uint32_t raw) {
    if (raw == 0) return 0.0;
    uint32_t integer = (raw >> 7) & 0xff;
    uint32_t frac = raw & 0x7f;
    return -1.0 * (double)((integer ^ 0xff) + 1) + (double)frac * 0.0078125;
}

static const char *lte_modulation_name(uint8_t order) {
    switch (order) {
        case 2: return "QPSK";
        case 4: return "16QAM";
        case 6: return "64QAM";
        case 8: return "256QAM";
        default: return "unknown";
    }
}

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void sleep_ms(unsigned int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static void set_last_error(const char *msg) {
    if (!msg) msg = "";
    snprintf(last_error, sizeof(last_error), "%s", msg);
}

static void reset_sample_cycle(void) {
    sample_events_seen = 0;
    sample_signal_seen = 0;
    sample_mac_seen = 0;
    sample_nr_seen = 0;
    sample_started_ms = monotonic_ms();
}

static int sample_has_enough_data(void) {
    if (require_mode == REQUIRE_ANY) return sample_events_seen > 0;
    if (require_mode == REQUIRE_MAC) return sample_mac_seen;
    if (require_mode == REQUIRE_NR) return sample_nr_seen;
    if (require_mode == REQUIRE_SIGNAL_MAC) return sample_signal_seen && sample_mac_seen;
    return sample_signal_seen;
}

static const char *require_mode_name(void) {
    if (require_mode == REQUIRE_ANY) return "any";
    if (require_mode == REQUIRE_MAC) return "mac";
    if (require_mode == REQUIRE_NR) return "nr";
    if (require_mode == REQUIRE_SIGNAL_MAC) return "signal-mac";
    return "signal";
}

static int lte_ant_slot(uint32_t earfcn, uint16_t pci, uint32_t cell_index) {
    int free_slot = -1;
    for (int i = 0; i < MAX_LTE_ANTENNA_CELLS; i++) {
        if (!state.lte_ant[i].valid) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (state.lte_ant[i].earfcn == earfcn &&
            state.lte_ant[i].pci == pci &&
            state.lte_ant[i].cell_index == cell_index) return i;
    }
    return free_slot >= 0 ? free_slot : (int)(cell_index % MAX_LTE_ANTENNA_CELLS);
}

static uint8_t cc_slot(uint8_t cc_id) {
    return cc_id < MAX_LTE_CC ? cc_id : (uint8_t)(cc_id % MAX_LTE_CC);
}

static void json_string(FILE *f, const char *s) {
    fputc('"', f);
    if (s) {
        for (; *s; s++) {
            unsigned char c = (unsigned char)*s;
            if (c == '"' || c == '\\') {
                fputc('\\', f);
                fputc(c, f);
            } else if (c == '\n') {
                fputs("\\n", f);
            } else if (c == '\r') {
                fputs("\\r", f);
            } else if (c == '\t') {
                fputs("\\t", f);
            } else if (c < 0x20) {
                fprintf(f, "\\u%04x", c);
            } else {
                fputc(c, f);
            }
        }
    }
    fputc('"', f);
}

static void combo_path(char *out, size_t out_len, const char *name) {
    if (!out_len) return;
    out[0] = '\0';
    if (!combo_dir) return;
    snprintf(out, out_len, "%s/%s", combo_dir, name);
}

static const char *log_name(uint16_t log_id) {
    switch (log_id) {
        case LOG_LTE_SMEAS: return "lte_ml1_serving_cell_meas";
        case LOG_LTE_NMEAS: return "lte_ml1_neighbor_measurements";
        case LOG_LTE_SMEAS_RESP: return "lte_ml1_serving_cell_meas_response";
        case LOG_LTE_CELL_INFO: return "lte_ml1_serving_cell_info";
        case LOG_LTE_RRC_OTA: return "lte_rrc_ota_message";
        case LOG_LTE_RRC_MIB: return "lte_rrc_mib";
        case LOG_LTE_RRC_SCELL: return "lte_rrc_serving_cell_info";
        case LOG_LTE_CA: return "lte_rrc_supported_ca_combos";
        case LOG_LTE_MAC_DL: return "lte_mac_dl_transport_block";
        case LOG_LTE_MAC_UL: return "lte_mac_ul_transport_block";
        case LOG_LTE_PHY_PDSCH_DEMAPPER_CONFIG: return "lte_phy_pdsch_demapper_config_candidate";
        case LOG_LTE_PHY_PDCCH_DECODING_RESULT: return "lte_phy_pdcch_decoding_result_candidate";
        case LOG_LTE_PHY_PDCCH_DECODING_RESULT2: return "lte_phy_pdcch_decoding_result2_candidate";
        case LOG_LTE_PHY_PUSCH_TX_REPORT: return "lte_phy_pusch_tx_report_candidate";
        case LOG_LTE_PHY_PUCCH_TX_REPORT: return "lte_phy_pucch_tx_report_candidate";
        case LOG_LTE_PHY_SRS_TX_REPORT: return "lte_phy_srs_tx_report_candidate";
        case LOG_LTE_PHY_RACH_TX_REPORT: return "lte_phy_rach_tx_report_candidate";
        case LOG_LTE_PHY_PUCCH_CSF_REPORT: return "lte_phy_pucch_csf_report_candidate";
        case LOG_LTE_PHY_PUSCH_CSF_REPORT: return "lte_phy_pusch_csf_report_candidate";
        case LOG_LTE_PHY_PDCCH_PHICH_INDICATION: return "lte_phy_pdcch_phich_indication_candidate";
        case LOG_LTE_PHY_GM_TX_REPORT: return "lte_phy_gm_tx_report_candidate";
        case LOG_LTE_PHY_PDSCH_STAT_INDICATION2: return "lte_phy_pdsch_stat_indication2_candidate";
        case LOG_LTE_PHY_PUSCH_STAT_INDICATION: return "lte_phy_pusch_stat_indication_candidate";
        case LOG_LTE_PHY_LEGACY_B175: return "lte_phy_legacy_b175_candidate";
        case LOG_LTE_PHY_LEGACY_B176: return "lte_phy_legacy_b176_candidate";
        case LOG_LTE_PHY_LEGACY_B177: return "lte_phy_legacy_b177_candidate";
        case LOG_LTE_PHY_LEGACY_B178: return "lte_phy_legacy_b178_candidate";
        case LOG_LTE_ML1_MAC_RAR_MSG1: return "lte_ml1_mac_rar_msg1_report";
        case LOG_LTE_ML1_MAC_RAR_MSG2: return "lte_ml1_mac_rar_msg2_report";
        case LOG_LTE_ML1_MAC_RAR_MSG3: return "lte_ml1_mac_msg3_report";
        case LOG_LTE_ML1_MAC_RAR_MSG4: return "lte_ml1_mac_msg4_report";
        case LOG_LTE_ML1_CONNECTED_INTRA_FREQ: return "lte_ml1_connected_intra_freq_meas";
        case LOG_LTE_ML1_NEIGHBOR_REQ_RESP: return "lte_ml1_neighbor_cell_meas_req_response";
        case LOG_LTE_ML1_SEARCH_REQ_RESP: return "lte_ml1_search_req_response";
        case LOG_LTE_ML1_CONNECTED_NEIGHBOR_REQ_RESP: return "lte_ml1_connected_neighbor_meas_req_response";
        case LOG_NR_NAS_5GMM_STATE: return "nr_nas_5gmm_state";
        case LOG_NR_RRC_OTA: return "nr_rrc_ota_message";
        case LOG_NR_MIB: return "nr_rrc_mib_info";
        case LOG_NR_RRC_SCELL: return "nr_rrc_serving_cell_info";
        case LOG_NR_RRC_CFG: return "nr_rrc_configuration_info";
        case LOG_NR_CA: return "nr_rrc_supported_ca_combos";
        case LOG_NR_MAC_UL_TB_STATS: return "nr_mac_ul_tb_stats_candidate";
        case LOG_NR_MAC_UL_PHYS_CH_SCHED: return "nr_mac_ul_physical_channel_schedule_candidate";
        case LOG_NR_MAC_PDSCH_STATS: return "nr_mac_pdsch_stats_candidate";
        case LOG_NR_MAC_RACH_ATTEMPT: return "nr_mac_rach_attempt";
        case LOG_NR_ML1_SERVING_CELL_BEAM_MGMT: return "nr_ml1_serving_cell_beam_management_candidate";
        case LOG_NR_ML1: return "nr_ml1_meas_database_update";
        default: return "unknown";
    }
}

static void track_log(uint16_t log_id, uint64_t qts) {
    int free_slot = -1;
    for (int i = 0; i < MAX_LOG_COUNTS; i++) {
        if (!log_counts[i].valid) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (log_counts[i].log_id == log_id) {
            log_counts[i].count++;
            log_counts[i].last_qts = qts;
            log_counts[i].last_seen_at = time(NULL);
            return;
        }
    }
    if (free_slot >= 0) {
        log_counts[free_slot].valid = 1;
        log_counts[free_slot].log_id = log_id;
        log_counts[free_slot].count = 1;
        log_counts[free_slot].last_qts = qts;
        log_counts[free_slot].last_seen_at = time(NULL);
    }
}

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

static double div0(double num, double den) {
    return den > 0.0 ? num / den : 0.0;
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

static void write_cc_ids(FILE *f, int *count_out) {
    int first = 1, count = 0;
    fputc('[', f);
    for (int i = 0; i < MAX_LTE_CC; i++) {
        if (!state.mac_dl[i].valid && !state.mac_ul[i].valid) continue;
        if (!first) fputc(',', f);
        fprintf(f, "%d", i);
        first = 0;
        count++;
    }
    fputc(']', f);
    if (count_out) *count_out = count;
}

static void write_lte_serving(FILE *f) {
    if (!state.lte_serving.valid) {
        fputs("{}", f);
        return;
    }
    const lte_serving_measurement_t *s = &state.lte_serving;
    fprintf(f,
            "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"version\":%u,"
            "\"rrc_release\":%u,\"earfcn\":%u,\"pci\":%u,"
            "\"rsrp_dbm\":%.2f,\"avg_rsrp_dbm\":%.2f,"
            "\"rssi_dbm\":%.2f,\"rsrq_db\":%.2f,\"avg_rsrq_db\":%.2f}",
            (long)s->updated_at, (unsigned long long)s->qts, s->version,
            s->rrc_release, s->earfcn, s->pci, s->rsrp_dbm,
            s->avg_rsrp_dbm, s->rssi_dbm, s->rsrq_db, s->avg_rsrq_db);
}

static void write_lte_serving_info(FILE *f) {
    if (!state.lte_info.valid) {
        fputs("{}", f);
        return;
    }
    const lte_serving_info_t *s = &state.lte_info;
    fprintf(f,
            "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"version\":%u,"
            "\"pci\":%u,\"dl_earfcn\":%u,\"ul_earfcn\":%u,\"band\":%u,"
            "\"dl_bandwidth_prb\":%u,\"ul_bandwidth_prb\":%u,"
            "\"cell_id\":%u,\"tac\":%u,\"mcc\":%u,\"mnc\":",
            (long)s->updated_at, (unsigned long long)s->qts, s->version,
            s->pci, s->dl_earfcn, s->ul_earfcn, s->band, s->dl_bandwidth_prb,
            s->ul_bandwidth_prb, s->cell_id, s->tac, s->mcc);
    json_string(f, s->mnc);
    fputc('}', f);
}

static void write_lte_per_antenna(FILE *f) {
    int first = 1;
    fputc('[', f);
    for (int i = 0; i < MAX_LTE_ANTENNA_CELLS; i++) {
        const lte_per_antenna_t *a = &state.lte_ant[i];
        if (!a->valid) continue;
        if (!first) fputc(',', f);
        fprintf(f,
                "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"version\":%u,"
                "\"earfcn\":%u,\"cell_index\":%u,\"valid_rx\":%u,"
                "\"rx_map\":%u,\"pci\":%u,\"serving_cell_index\":%u,"
                "\"is_serving_cell\":%s,\"sfn\":%u,\"subframe\":%u,"
                "\"rsrp_dbm\":[%.2f,%.2f,%.2f,%.2f],"
                "\"combined_rsrp_dbm\":%.2f,\"filtered_rsrp_dbm\":%.2f,"
                "\"rsrq_db\":[%.2f,%.2f,%.2f,%.2f],"
                "\"combined_rsrq_db\":%.2f,\"filtered_rsrq_db\":%.2f,"
                "\"rssi_dbm\":[%.2f,%.2f,%.2f,%.2f],"
                "\"combined_rssi_dbm\":%.2f,"
                "\"snr_db\":[%.2f,%.2f,%.2f,%.2f],"
                "\"projected_sir_db\":%.2f,\"post_ic_rsrq_db\":%.2f,"
                "\"cinr_raw\":[%d,%d,%d,%d]}",
                (long)a->updated_at, (unsigned long long)a->qts, a->version,
                a->earfcn, a->cell_index, a->valid_rx, a->rx_map, a->pci,
                a->serving_cell_index, a->is_serving_cell ? "true" : "false",
                a->sfn, a->subframe, a->rsrp_dbm[0], a->rsrp_dbm[1],
                a->rsrp_dbm[2], a->rsrp_dbm[3], a->combined_rsrp_dbm,
                a->filtered_rsrp_dbm, a->rsrq_db[0], a->rsrq_db[1],
                a->rsrq_db[2], a->rsrq_db[3], a->combined_rsrq_db,
                a->filtered_rsrq_db, a->rssi_dbm[0], a->rssi_dbm[1],
                a->rssi_dbm[2], a->rssi_dbm[3], a->combined_rssi_dbm,
                a->snr_db[0], a->snr_db[1], a->snr_db[2], a->snr_db[3],
                a->projected_sir_db, a->post_ic_rsrq_db, a->cinr_raw[0],
                a->cinr_raw[1], a->cinr_raw[2], a->cinr_raw[3]);
        first = 0;
    }
    fputc(']', f);
}

static void write_lte_mac(FILE *f) {
    fputs("{\"dl_by_cc\":", f);
    fputc('[', f);
    int first = 1;
    for (int i = 0; i < MAX_LTE_CC; i++) {
        const lte_mac_dl_t *m = &state.mac_dl[i];
        if (!m->valid) continue;
        if (!first) fputc(',', f);
        fprintf(f,
                "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"version\":%u,"
                "\"cc_id\":%u,\"sfn\":%u,\"subframe\":%u,"
                "\"rnti_type\":%u,\"harq_id\":%u,\"tbs_bytes\":%u,"
                "\"padding_bytes\":%u,\"num_sdu\":%u,\"num_lcid\":%u}",
                (long)m->updated_at, (unsigned long long)m->qts, m->version,
                m->cc_id, m->sfn, m->subframe, m->rnti_type, m->harq_id,
                m->tbs_bytes, m->padding_bytes, m->num_sdu, m->num_lcid);
        first = 0;
    }
    fputs("],\"ul_by_cc\":", f);
    fputc('[', f);
    first = 1;
    for (int i = 0; i < MAX_LTE_CC; i++) {
        const lte_mac_ul_t *m = &state.mac_ul[i];
        if (!m->valid) continue;
        if (!first) fputc(',', f);
        fprintf(f,
                "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"version\":%u,"
                "\"cc_id\":%u,\"sfn\":%u,\"subframe\":%u,"
                "\"rnti_type\":%u,\"harq_id\":%u,\"grant\":%u,"
                "\"rlc_pdus\":%u,\"padding_bytes\":%u,"
                "\"bsr_event\":%u,\"bsr_trigger\":%u}",
                (long)m->updated_at, (unsigned long long)m->qts, m->version,
                m->cc_id, m->sfn, m->subframe, m->rnti_type, m->harq_id,
                m->grant, m->rlc_pdus, m->padding_bytes, m->bsr_event,
                m->bsr_trigger);
        first = 0;
    }
    fputs("],\"phy_pusch_tx\":", f);
    if (state.pusch.valid) {
        const lte_phy_pusch_t *p = &state.pusch;
        fprintf(f,
                "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"version\":%u,"
                "\"tti\":%u,\"sfn_guess\":%u,\"subframe_guess\":%u,"
                "\"carrier_id\":%u,\"rb_start_slot0\":%u,"
                "\"rb_start_slot1\":%u,\"rb_count\":%u,"
                "\"tb_size\":%u,\"grant\":%u,\"coding_rate\":%.3f,"
                "\"pusch_mod_order\":%u,\"pusch_modulation\":",
                (long)p->updated_at, (unsigned long long)p->qts,
                p->version, p->tti, p->sfn, p->subframe, p->carrier_id,
                p->rb_start_slot0, p->rb_start_slot1, p->rb_count,
                p->tb_size, p->grant, p->coding_rate, p->pusch_mod_order);
        json_string(f, lte_modulation_name(p->pusch_mod_order));
        fprintf(f,
                ",\"tx_power_raw\":%u,\"tx_power_dbm_candidate\":%d}",
                p->tx_power_raw, p->tx_power_dbm_candidate);
    } else {
        fputs("{}", f);
    }
    fputc('}', f);
}

static void write_combo_state(FILE *f, const combo_state_t *c) {
    if (!c->valid) {
        fputs("{}", f);
        return;
    }
    fprintf(f, "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"format\":",
            (long)c->updated_at, (unsigned long long)c->qts);
    json_string(f, c->format);
    fprintf(f, ",\"bytes\":%zu", c->bytes);
    if (c->path[0]) {
        fputs(",\"file\":", f);
        json_string(f, c->path);
    }
    fputc('}', f);
}

static void write_lte_ca(FILE *f) {
    int cc_count = 0;
    fputs("{\"observed_cc_ids\":", f);
    write_cc_ids(f, &cc_count);
    fprintf(f, ",\"observed_component_count\":%d,\"supported_combos\":", cc_count);
    write_combo_state(f, &state.lte_combo);
    fputc('}', f);
}

static void write_nr_serving(FILE *f) {
    if (!state.nr_serving.valid) {
        fputs("{}", f);
        return;
    }
    const nr_serving_info_t *s = &state.nr_serving;
    fprintf(f, "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"version\":",
            (long)s->updated_at, (unsigned long long)s->qts);
    json_string(f, s->version);
    fprintf(f,
            ",\"pci\":%u,\"dl_nr_arfcn\":%u,\"ul_nr_arfcn\":%u,\"band\":%u}",
            s->pci, s->dl_nr_arfcn, s->ul_nr_arfcn, s->band);
}

static void write_nullable_int(FILE *f, int value) {
    if (value < 0) fputs("null", f);
    else fprintf(f, "%d", value);
}

static void write_nr_layers(FILE *f) {
    fputc('[', f);
    int first_layer = 1;
    for (int i = 0; i < MAX_NR_LAYERS; i++) {
        const nr_layer_t *l = &state.nr_layers[i];
        if (!l->valid) continue;
        if (!first_layer) fputc(',', f);
        fprintf(f,
                "{\"updated_at\":%ld,\"qxdm_ts\":%llu,\"version\":",
                (long)l->updated_at, (unsigned long long)l->qts);
        json_string(f, state.nr_ml1_version);
        fprintf(f,
                ",\"layer\":%u,\"nr_arfcn\":%u,\"num_cells\":%u,"
                "\"serving_cell_index\":%u,\"serving_pci\":%u,"
                "\"serving_ssb\":%u,\"ssb_periodicity\":%u",
                l->layer, l->nr_arfcn, l->num_cells, l->serving_cell_index,
                l->serving_pci, l->serving_ssb, state.nr_ssb_periodicity);
        if (l->has_cc_id) fprintf(f, ",\"cc_id\":%u", l->cc_id);
        fputs(",\"serving_rsrp_dbm\":[", f);
        for (uint8_t rx = 0; rx < l->serving_rsrp_count; rx++) {
            if (rx) fputc(',', f);
            fprintf(f, "%.2f", l->serving_rsrp_dbm[rx]);
        }
        fputs("],\"rx_beam\":[", f);
        write_nullable_int(f, l->rx_beam[0]);
        fputc(',', f);
        write_nullable_int(f, l->rx_beam[1]);
        fprintf(f, "],\"rfic_id\":%u,\"subarray\":[%u,%u],\"cells\":[",
                l->rfic_id, l->subarray[0], l->subarray[1]);
        int first_cell = 1;
        for (int c = 0; c < MAX_NR_CELLS; c++) {
            const nr_cell_t *cell = &l->cells[c];
            if (!cell->valid) continue;
            if (!first_cell) fputc(',', f);
            fprintf(f,
                    "{\"index\":%u,\"pci\":%u,\"pbch_sfn\":%u,"
                    "\"num_beams\":%u,\"rsrp_dbm\":%.2f,\"rsrq_db\":%.2f,"
                    "\"beams\":[",
                    cell->index, cell->pci, cell->pbch_sfn, cell->num_beams,
                    cell->rsrp_dbm, cell->rsrq_db);
            int first_beam = 1;
            for (int b = 0; b < MAX_NR_BEAMS; b++) {
                const nr_beam_t *beam = &cell->beams[b];
                if (!beam->valid) continue;
                if (!first_beam) fputc(',', f);
                fprintf(f,
                        "{\"index\":%u,\"ssb_index\":%u,\"rx_beam\":[",
                        beam->index, beam->ssb_index);
                write_nullable_int(f, beam->rx_beam[0]);
                fputc(',', f);
                write_nullable_int(f, beam->rx_beam[1]);
                fprintf(f,
                        "],\"ssb_ref_timing\":%llu,\"rsrp_dbm\":[%.2f,%.2f]",
                        (unsigned long long)beam->ssb_ref_timing,
                        beam->rsrp_dbm[0], beam->rsrp_dbm[1]);
                if (beam->has_rsrq) {
                    fprintf(f, ",\"rsrq_db\":[%.2f,%.2f]", beam->rsrq_db[0], beam->rsrq_db[1]);
                }
                fprintf(f,
                        ",\"filtered_nr2nr_rsrp_dbm\":%.2f,"
                        "\"filtered_nr2nr_rsrq_db\":%.2f,"
                        "\"filtered_l2nr_rsrp_dbm\":%.2f,"
                        "\"filtered_l2nr_rsrq_db\":%.2f}",
                        beam->filtered_nr2nr_rsrp_dbm, beam->filtered_nr2nr_rsrq_db,
                        beam->filtered_l2nr_rsrp_dbm, beam->filtered_l2nr_rsrq_db);
                first_beam = 0;
            }
            fputs("]}", f);
            first_cell = 0;
        }
        fputs("]}", f);
        first_layer = 0;
    }
    fputc(']', f);
}

static void write_nr_ul_tb_stats(FILE *f) {
    if (!state.nr_ul_tb_stats.valid) {
        fputs("{}", f);
        return;
    }
    const nr_mac_ul_tb_stats_t *s = &state.nr_ul_tb_stats;
    fprintf(f,
            "{\"updated_at\":%ld,\"qxdm_ts\":%llu,"
            "\"confidence\":\"layout_v%u_%u_decoded\","
            "\"version\":\"%u.%u\",\"record_index\":%u,\"record_count\":%u,"
            "\"tb_new_tx_bytes\":%llu,\"tb_retx_bytes\":%llu,"
            "\"num_mcs\":%llu,\"avg_mcs_candidate\":%.2f,"
            "\"num_prb\":%llu,\"avg_prb_candidate\":%.2f,"
            "\"phr\":%llu,\"avg_phr_candidate\":%.2f,"
            "\"total_power\":%llu,\"num_new_tx_tb\":%u,"
            "\"num_retx_tb\":%u,\"retx_tb_ratio\":%.4f,"
            "\"num_dtx\":%u,\"num_ri\":%u,\"ri\":%u,"
            "\"avg_ri_candidate\":%.2f,\"num_cqi\":%u,\"cqi\":%u,"
            "\"avg_cqi_candidate\":%.2f,\"num_phr\":%u,"
            "\"tpc_accum\":%u,\"num_ulsch_sched\":%u,"
            "\"num_no_ulsch_sched\":%u,\"ulsch_sched_ratio\":%.4f,"
            "\"pcmax_raw\":%u,\"pcmax_dbm_candidate\":%.1f,"
            "\"flush_gap_count\":%u}",
            (long)s->updated_at, (unsigned long long)s->qts,
            s->major, s->minor, s->major, s->minor, s->record_index,
            s->record_count, (unsigned long long)s->tb_new_tx_bytes,
            (unsigned long long)s->tb_retx_bytes,
            (unsigned long long)s->num_mcs, s->avg_mcs_candidate,
            (unsigned long long)s->num_prb, s->avg_prb_candidate,
            (unsigned long long)s->phr, s->avg_phr_candidate,
            (unsigned long long)s->total_power, s->num_new_tx_tb,
            s->num_retx_tb, s->retx_tb_ratio, s->num_dtx, s->num_ri,
            s->ri, s->avg_ri_candidate, s->num_cqi, s->cqi,
            s->avg_cqi_candidate, s->num_phr, s->tpc_accum,
            s->num_ulsch_sched, s->num_no_ulsch_sched,
            s->ulsch_sched_ratio, s->pcmax_raw, s->pcmax_dbm_candidate,
            s->flush_gap_count);
}

static void write_nr_pdsch_stats_item(FILE *f, const nr_mac_pdsch_stats_t *s) {
    fprintf(f,
            "{\"updated_at\":%ld,\"qxdm_ts\":%llu,"
            "\"confidence\":\"layout_v%u_%u_decoded\","
            "\"version\":\"%u.%u\",\"record_index\":%u,\"record_count\":%u,"
            "\"carrier_id\":%u,\"num_slots_elapsed\":%u,"
            "\"num_pdsch_decode\":%u,\"num_crc_pass_tb\":%u,"
            "\"num_crc_fail_tb\":%u,\"dl_bler\":%.4f,"
            "\"num_retx\":%u,\"retx_ratio\":%.4f,"
            "\"ack_as_nack\":%u,\"harq_failure\":%u,"
            "\"crc_pass_tb_bytes\":%llu,\"crc_fail_tb_bytes\":%llu,"
            "\"byte_error_ratio\":%.4f,\"tb_bytes\":%llu,"
            "\"padding_bytes\":%llu,\"retx_bytes\":%llu}",
            (long)s->updated_at, (unsigned long long)s->qts,
            s->major, s->minor, s->major, s->minor, s->record_index,
            s->record_count, s->carrier_id, s->num_slots_elapsed,
            s->num_pdsch_decode, s->num_crc_pass_tb, s->num_crc_fail_tb,
            s->dl_bler, s->num_retx, s->retx_ratio, s->ack_as_nack,
            s->harq_failure, (unsigned long long)s->crc_pass_tb_bytes,
            (unsigned long long)s->crc_fail_tb_bytes, s->byte_error_ratio,
            (unsigned long long)s->tb_bytes,
            (unsigned long long)s->padding_bytes,
            (unsigned long long)s->retx_bytes);
}

static void write_nr_mac(FILE *f) {
    fputs("{\"ul_tb_stats\":", f);
    write_nr_ul_tb_stats(f);
    fputs(",\"pdsch_stats_by_cc\":[", f);
    int first = 1;
    for (int i = 0; i < MAX_LTE_CC; i++) {
        const nr_mac_pdsch_stats_t *s = &state.nr_pdsch_stats[i];
        if (!s->valid) continue;
        if (!first) fputc(',', f);
        write_nr_pdsch_stats_item(f, s);
        first = 0;
    }
    fputs("]}", f);
}

static void write_lte_phy(FILE *f) {
    fputs("{\"pusch_tx_candidate\":", f);
    if (state.pusch.valid) {
        const lte_phy_pusch_t *p = &state.pusch;
        fprintf(f,
                "{\"updated_at\":%ld,\"qxdm_ts\":%llu,"
                "\"confidence\":\"layout_decoded\","
                "\"version\":%u,\"header_word\":%u,\"serving_cell_id\":%u,"
                "\"dispatch_sfn\":%u,\"dispatch_subframe\":%u,"
                "\"record_index\":%u,\"record_count\":%u,"
                "\"tti\":%u,\"sfn_guess\":%u,\"subframe_guess\":%u,"
                "\"carrier_id\":%u,\"ack_flag\":%u,\"cqi_flag\":%u,"
                "\"ri_flag\":%u,\"frequency_hopping\":%u,\"retx_index\":%u,"
                "\"rv\":%u,\"mirror_hopping\":%u,"
                "\"ra_type\":%u,\"rb_start_slot0\":%u,"
                "\"rb_start_slot1\":%u,\"rb_count\":%u,"
                "\"tb_size\":%u,\"grant\":%u,\"coding_rate_raw\":%u,"
                "\"coding_rate\":%.3f,\"pusch_mod_order\":%u,"
                "\"pusch_modulation\":",
                (long)p->updated_at, (unsigned long long)p->qts,
                p->version, p->header_word, p->serving_cell_id,
                p->dispatch_sfn, p->dispatch_subframe,
                p->record_index, p->record_count, p->tti, p->sfn,
                p->subframe, p->carrier_id, p->ack_flag, p->cqi_flag,
                p->ri_flag, p->frequency_hopping, p->retx_index, p->rv,
                p->mirror_hopping, p->ra_type, p->rb_start_slot0,
                p->rb_start_slot1, p->rb_count, p->tb_size, p->grant,
                p->coding_rate_raw, p->coding_rate, p->pusch_mod_order);
        json_string(f, lte_modulation_name(p->pusch_mod_order));
        fprintf(f,
                ",\"mod_gain_cluster_raw\":%u,\"tx_cqi_raw\":%u,"
                "\"tx_power_raw\":%u,\"tx_power_dbm_candidate\":%d}",
                p->mod_gain_cluster_raw, p->tx_cqi_raw, p->tx_power_raw,
                p->tx_power_dbm_candidate);
    } else {
        fputs("{}", f);
    }
    fputs(",\"pdsch_stat_candidate\":", f);
    if (state.pdsch.valid) {
        const lte_phy_pdsch_t *p = &state.pdsch;
        if (p->decoded) {
            fprintf(f,
                    "{\"updated_at\":%ld,\"qxdm_ts\":%llu,"
                    "\"confidence\":\"layout_v36_decoded\","
                    "\"version\":%u,\"header_word\":%u,"
                    "\"record_index\":%u,\"record_count\":%u,"
                    "\"sfn\":%u,\"subframe\":%u,\"num_rbs\":%u,"
                    "\"num_layers\":%u,\"num_transport_blocks\":%u,"
                    "\"serving_cell_id\":%u,\"hsic_enabled\":%u,"
                    "\"crc_pass_total\":%llu,\"crc_fail_total\":%llu,"
                    "\"dl_bler\":%.4f,\"transport_blocks\":[",
                    (long)p->updated_at, (unsigned long long)p->qts,
                    p->version, p->header_word, p->record_index,
                    p->record_count, p->sfn, p->subframe, p->num_rbs,
                    p->num_layers, p->num_transport_blocks,
                    p->serving_cell_id, p->hsic_enabled,
                    p->crc_pass_total, p->crc_fail_total, p->dl_bler);
            int first_tb = 1;
            for (int i = 0; i < 2; i++) {
                const lte_phy_pdsch_tb_t *tb = &p->tb[i];
                if (!tb->valid) continue;
                if (!first_tb) fputc(',', f);
                fprintf(f,
                        "{\"tb_index\":%u,\"harq_id\":%u,\"rv\":%u,"
                        "\"ndi\":%u,\"crc_result\":%u,\"rnti_type\":%u,"
                        "\"discarded_retx_present\":%u,"
                        "\"did_recombining\":%u,\"tb_size\":%u,"
                        "\"mcs\":%u,\"num_rbs\":%u,"
                        "\"modulation_type\":%u,\"modulation\":",
                        tb->tb_index, tb->harq_id, tb->rv, tb->ndi,
                        tb->crc_result, tb->rnti_type,
                        tb->discarded_retx_present, tb->did_recombining,
                        tb->tb_size, tb->mcs, tb->num_rbs,
                        tb->modulation_type);
                json_string(f, lte_modulation_name(tb->modulation_type));
                fprintf(f, ",\"qed2_interim\":%u}", tb->qed2_interim);
                first_tb = 0;
            }
            fputs("]}", f);
        } else {
            fprintf(f,
                    "{\"updated_at\":%ld,\"qxdm_ts\":%llu,"
                    "\"confidence\":\"candidate_unconfirmed\","
                    "\"version\":%u,\"subversion\":%u,\"header_word\":%u,"
                    "\"record_index\":%u,\"record_count\":%u,"
                    "\"tti_guess\":%u,\"mcs_candidate_raw\":%u,"
                    "\"field_0_raw\":%u,\"field_2_raw\":%u,"
                    "\"field_4_raw\":%u,\"field_8_raw\":%u,"
                    "\"field_12_raw\":%u,\"field_16_raw\":%u,"
                    "\"field_18_raw\":%u,\"field_20_raw\":%u,"
                    "\"field_24_raw\":%u,\"field_28_raw\":%u}",
                    (long)p->updated_at, (unsigned long long)p->qts,
                    p->version, p->subversion, p->header_word,
                    p->record_index, p->record_count, p->field_0_raw,
                    p->mcs_candidate_raw, p->field_0_raw, p->field_2_raw,
                    p->field_4_raw, p->field_8_raw, p->field_12_raw,
                    p->field_16_raw, p->field_18_raw, p->field_20_raw,
                    p->field_24_raw, p->field_28_raw);
        }
    } else {
        fputs("{}", f);
    }
    fputc('}', f);
}

static void write_missing_group(FILE *f, const missing_metric_t *items, size_t count) {
    fputc('{', f);
    for (size_t i = 0; i < count; i++) {
        if (i) fputc(',', f);
        json_string(f, items[i].name);
        fputs(":{\"value\":null,\"status\":\"not_decoded\",\"source_candidates\":[", f);
        json_string(f, items[i].source_candidates);
        fputs("]}", f);
    }
    fputc('}', f);
}

static void write_missing_metrics(FILE *f) {
    fputs("{\"lte\":", f);
    write_missing_group(f, lte_missing_metrics, sizeof(lte_missing_metrics) / sizeof(lte_missing_metrics[0]));
    fputs(",\"nr\":", f);
    write_missing_group(f, nr_missing_metrics, sizeof(nr_missing_metrics) / sizeof(nr_missing_metrics[0]));
    fputc('}', f);
}

static void write_log_counts(FILE *f) {
    int first = 1;
    fputc('[', f);
    for (int i = 0; i < MAX_LOG_COUNTS; i++) {
        const log_count_t *c = &log_counts[i];
        if (!c->valid) continue;
        if (!first) fputc(',', f);
        fprintf(f, "{\"log_id\":\"0x%04X\",\"name\":", c->log_id);
        json_string(f, log_name(c->log_id));
        fprintf(f, ",\"count\":%llu,\"last_seen_at\":%ld,\"last_qxdm_ts\":%llu}",
                c->count, (long)c->last_seen_at, (unsigned long long)c->last_qts);
        first = 0;
    }
    fputc(']', f);
}

static int write_snapshot(int is_running) {
    if (!snapshot_file) return 0;
    char tmp[512];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", snapshot_file);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        set_last_error("snapshot path too long");
        return -1;
    }
    FILE *f = fopen(tmp, "w");
    if (!f) {
        snprintf(last_error, sizeof(last_error), "open snapshot tmp failed errno=%d", errno);
        return -1;
    }

    time_t now = time(NULL);
    long uptime = runtime_start ? (long)(now - runtime_start) : 0;
    fprintf(f, "{\"updated_at\":%ld,\"stale_after_ms\":%d,\"lte\":{",
            (long)now, stale_after_ms);
    fputs("\"serving_cell\":", f);
    write_lte_serving(f);
    fputs(",\"serving_info\":", f);
    write_lte_serving_info(f);
    fputs(",\"per_antenna\":", f);
    write_lte_per_antenna(f);
    fputs(",\"mac\":", f);
    write_lte_mac(f);
    fputs(",\"phy\":", f);
    write_lte_phy(f);
    fputs(",\"ca\":", f);
    write_lte_ca(f);
    fputs("},\"nr\":{\"serving_cell\":", f);
    write_nr_serving(f);
    fputs(",\"layers\":", f);
    write_nr_layers(f);
    fputs(",\"mac\":", f);
    write_nr_mac(f);
    fputs(",\"ca\":{\"supported_combos\":", f);
    write_combo_state(f, &state.nr_combo);
    fputs("}},\"missing_metrics\":", f);
    write_missing_metrics(f);
    fputs(",\"runtime\":{\"running\":", f);
    fputs(is_running ? "true" : "false", f);
    fprintf(f, ",\"uptime_s\":%ld,\"events_seen\":%llu,"
            "\"snapshot_interval_ms\":%d,\"sample_window_ms\":%d,"
            "\"sample_min_ms\":%d,\"oneshot\":%s,\"require\":",
            uptime, events_seen, snapshot_interval_ms, sample_window_ms,
            sample_min_ms, opt_oneshot ? "true" : "false");
    json_string(f, require_mode_name());
    fprintf(f, ",\"sample_events_seen\":%llu,\"sample_signal_seen\":%s,"
            "\"sample_mac_seen\":%s,\"diag_stream_active\":%s,"
            "\"sample_nr_seen\":%s,"
            "\"gui_lite\":%s,\"nice_increment\":%d,\"last_error\":",
            sample_events_seen, sample_signal_seen ? "true" : "false",
            sample_mac_seen ? "true" : "false",
            log_stream_enabled ? "true" : "false",
            sample_nr_seen ? "true" : "false",
            opt_gui_lite ? "true" : "false", nice_increment);
    json_string(f, last_error);
    fputs(",\"log_counts\":", f);
    write_log_counts(f);
    fputs("}}\n", f);

    if (fflush(f) != 0) {
        snprintf(last_error, sizeof(last_error), "flush snapshot failed errno=%d", errno);
    }
    fsync(fileno(f));
    if (fclose(f) != 0) {
        snprintf(last_error, sizeof(last_error), "close snapshot failed errno=%d", errno);
        return -1;
    }
    if (rename(tmp, snapshot_file) != 0) {
        snprintf(last_error, sizeof(last_error), "rename snapshot failed errno=%d", errno);
        return -1;
    }
    return 0;
}

static int set_log_stream_state(int client_id, uint16 logs[], int count, int enable, int *enabled_state) {
    if (*enabled_state == enable) return 0;
    int err = diag_log_stream_config(client_id, enable ? ENABLE : DISABLE, logs, count);
    if (err != DIAG_DCI_NO_ERROR) {
        snprintf(last_error, sizeof(last_error), "diag_log_stream_config %s failed err=%d errno=%d",
                 enable ? "enable" : "disable", err, errno);
        return err;
    }
    *enabled_state = enable;
    if (enable) reset_sample_cycle();
    return DIAG_DCI_NO_ERROR;
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
        case LOG_LTE_PHY_SRS_TX_REPORT:
        case LOG_LTE_PHY_RACH_TX_REPORT:
        case LOG_LTE_PHY_PUCCH_CSF_REPORT:
        case LOG_LTE_PHY_PUSCH_CSF_REPORT:
        case LOG_LTE_PHY_PDCCH_PHICH_INDICATION:
        case LOG_LTE_PHY_GM_TX_REPORT:
        case LOG_LTE_PHY_PDSCH_STAT_INDICATION2:
        case LOG_LTE_PHY_PUSCH_STAT_INDICATION:
        case LOG_LTE_PHY_LEGACY_B175:
        case LOG_LTE_PHY_LEGACY_B176:
        case LOG_LTE_PHY_LEGACY_B177:
        case LOG_LTE_PHY_LEGACY_B178:
        case LOG_NR_MAC_UL_TB_STATS:
        case LOG_NR_MAC_UL_PHYS_CH_SCHED:
        case LOG_NR_MAC_PDSCH_STATS:
        case LOG_NR_ML1_SERVING_CELL_BEAM_MGMT:
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
    double v_rsrp = rsrp(meas_rsrp & 0xfff);
    double v_avg_rsrp = rsrp(avg_rsrp & 0xfff);
    double v_rssi = rssi((rssi_word >> 10) & 0x7ff);
    double v_rsrq = rsrq(rsrq_word & 0x3ff);
    double v_avg_rsrq = rsrq((rsrq_word >> 20) & 0x3ff);
    state.lte_serving.valid = 1;
    state.lte_serving.version = ver;
    state.lte_serving.rrc_release = rrc;
    state.lte_serving.earfcn = earfcn;
    state.lte_serving.pci = pci_prio & 0x1ff;
    state.lte_serving.rsrp_dbm = v_rsrp;
    state.lte_serving.avg_rsrp_dbm = v_avg_rsrp;
    state.lte_serving.rssi_dbm = v_rssi;
    state.lte_serving.rsrq_db = v_rsrq;
    state.lte_serving.avg_rsrq_db = v_avg_rsrq;
    state.lte_serving.qts = qts;
    state.lte_serving.updated_at = time(NULL);
    sample_signal_seen = 1;
    if (opt_stream_json) {
        json_prefix(log_id, qts, "LTE", "serving_cell_measurement");
        printf(",\"version\":%u,\"rrc_release\":%u,\"earfcn\":%u,\"pci\":%u,"
               "\"rsrp_dbm\":%.2f,\"avg_rsrp_dbm\":%.2f,\"rssi_dbm\":%.2f,"
               "\"rsrq_db\":%.2f,\"avg_rsrq_db\":%.2f}\n",
               ver, rrc, earfcn, pci_prio & 0x1ff, v_rsrp, v_avg_rsrp,
               v_rssi, v_rsrq, v_avg_rsrq);
    }
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
                double v_rsrp[4] = {
                    rsrp(getbits32(words, 12, 10, 12)),
                    rsrp(getbits32(words, 12, 44, 12)),
                    rsrp(getbits32(words, 12, 76, 12)),
                    rsrp(getbits32(words, 12, 96, 12))
                };
                double v_combined_rsrp = rsrp(getbits32(words, 12, 108, 12)) + 40.0;
                double v_filtered_rsrp = rsrp(getbits32(words, 12, 140, 12));
                double v_rsrq[4] = {
                    rsrq(getbits32(words, 12, 160, 10)),
                    rsrq(getbits32(words, 12, 180, 10)),
                    rsrq(getbits32(words, 12, 202, 10)),
                    rsrq(getbits32(words, 12, 212, 10))
                };
                double v_combined_rsrq = rsrq(getbits32(words, 12, 224, 10));
                double v_filtered_rsrq = rsrq(getbits32(words, 12, 244, 10));
                double v_rssi[4] = {
                    rssi(getbits32(words, 12, 256, 11)),
                    rssi(getbits32(words, 12, 267, 11)),
                    rssi(getbits32(words, 12, 288, 11)),
                    rssi(getbits32(words, 12, 299, 11))
                };
                double v_combined_rssi = rssi(getbits32(words, 12, 320, 11));
                double v_snr[4] = {
                    snr_lte(getbits32(snr_words, 2, 0, 9)),
                    snr_lte(getbits32(snr_words, 2, 9, 9)),
                    snr_lte(getbits32(snr_words, 2, 32, 9)),
                    snr_lte(getbits32(snr_words, 2, 42, 8))
                };
                double v_projected_sir = (double)prj_sir / 16.0;
                double v_post_ic_rsrq = (double)post_ic_rsrq * 0.0625 - 30.0;
                int slot = lte_ant_slot(earfcn, meta0 & 0x1ff, idx);
                lte_per_antenna_t *a = &state.lte_ant[slot];
                memset(a, 0, sizeof(*a));
                a->valid = 1;
                a->version = sub_ver;
                a->earfcn = earfcn;
                a->cell_index = idx;
                a->valid_rx = valid_rx;
                a->rx_map = rx_map;
                a->pci = meta0 & 0x1ff;
                a->serving_cell_index = (meta0 >> 9) & 7;
                a->is_serving_cell = (meta0 & 0x1000) ? 1 : 0;
                a->sfn = meta2 & 0x3ff;
                a->subframe = (meta2 >> 10) & 0xf;
                for (int rx = 0; rx < 4; rx++) {
                    a->rsrp_dbm[rx] = v_rsrp[rx];
                    a->rsrq_db[rx] = v_rsrq[rx];
                    a->rssi_dbm[rx] = v_rssi[rx];
                    a->snr_db[rx] = v_snr[rx];
                }
                a->combined_rsrp_dbm = v_combined_rsrp;
                a->filtered_rsrp_dbm = v_filtered_rsrp;
                a->combined_rsrq_db = v_combined_rsrq;
                a->filtered_rsrq_db = v_filtered_rsrq;
                a->combined_rssi_dbm = v_combined_rssi;
                a->projected_sir_db = v_projected_sir;
                a->post_ic_rsrq_db = v_post_ic_rsrq;
                a->cinr_raw[0] = cinr0;
                a->cinr_raw[1] = cinr1;
                a->cinr_raw[2] = cinr2;
                a->cinr_raw[3] = cinr3;
                a->qts = qts;
                a->updated_at = time(NULL);
                sample_signal_seen = 1;
                if (opt_stream_json) {
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
                           v_rsrp[0], v_rsrp[1], v_rsrp[2], v_rsrp[3],
                           v_combined_rsrp, v_filtered_rsrp,
                           v_rsrq[0], v_rsrq[1], v_rsrq[2], v_rsrq[3],
                           v_combined_rsrq, v_filtered_rsrq,
                           v_rssi[0], v_rssi[1], v_rssi[2], v_rssi[3],
                           v_combined_rssi, v_snr[0], v_snr[1], v_snr[2], v_snr[3],
                           v_projected_sir, v_post_ic_rsrq, cinr0, cinr1, cinr2, cinr3);
                }
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
                (void)meta1;
                if (opt_stream_json) {
                    json_prefix(log_id, qts, "LTE", "per_antenna_measurement_raw");
                    printf(",\"version\":%u,\"earfcn\":%u,\"cell_index\":%u,\"valid_rx\":%u,"
                           "\"rx_map\":%u,\"pci_guess\":%u,\"serving_cell_index_guess\":%u,"
                           "\"is_serving_cell_guess\":%s,\"meta_words\":[%u,%u,%u],\"cell_hex\":\"",
                           sub_ver, earfcn, idx, valid_rx, rx_map, meta0 & 0x1ff,
                           (meta0 >> 9) & 7, (meta0 & 0x1000) ? "true" : "false",
                           meta0, meta1, meta2);
                    hexprint(stdout, sub + cell_pos, have);
                    printf("\"}\n");
                }
                parsed = 1;
            }
        } else if (opt_debug) {
            if (opt_stream_json) {
                json_prefix(log_id, qts, "LTE", "per_antenna_measurement_unknown");
                printf(",\"subpacket_id\":%u,\"subpacket_version\":%u,\"subpacket_size\":%u,\"payload_hex\":\"",
                       sub_id, sub_ver, sub_size);
                hexprint(stdout, sub, sub_len < 192 ? sub_len : 192);
                printf("\"}\n");
            }
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
    state.lte_info.valid = 1;
    state.lte_info.version = ver;
    state.lte_info.pci = pci;
    state.lte_info.dl_earfcn = dl;
    state.lte_info.ul_earfcn = ul;
    state.lte_info.band = band;
    state.lte_info.dl_bandwidth_prb = dlbw;
    state.lte_info.ul_bandwidth_prb = ulbw;
    state.lte_info.cell_id = cid;
    state.lte_info.tac = tac;
    state.lte_info.mcc = mcc;
    snprintf(state.lte_info.mnc, sizeof(state.lte_info.mnc), "%0*u", mncd == 3 ? 3 : 2, mnc);
    state.lte_info.qts = qts;
    state.lte_info.updated_at = time(NULL);
    sample_signal_seen = 1;
    if (opt_stream_json) {
        json_prefix(log_id, qts, "LTE", "serving_cell_info");
        printf(",\"version\":%u,\"pci\":%u,\"dl_earfcn\":%u,\"ul_earfcn\":%u,"
               "\"band\":%u,\"dl_bandwidth_prb\":%u,\"ul_bandwidth_prb\":%u,"
               "\"cell_id\":%u,\"tac\":%u,\"mcc\":%u,\"mnc\":\"%0*u\"}\n",
               ver, pci, dl, ul, band, dlbw, ulbw, cid, tac, mcc, mncd == 3 ? 3 : 2, mnc);
    }
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
    state.nr_serving.valid = 1;
    snprintf(state.nr_serving.version, sizeof(state.nr_serving.version), "%u.%u", rel_maj, rel_min);
    state.nr_serving.pci = pci;
    state.nr_serving.dl_nr_arfcn = dl;
    state.nr_serving.ul_nr_arfcn = ul;
    state.nr_serving.band = band;
    state.nr_serving.qts = qts;
    state.nr_serving.updated_at = time(NULL);
    sample_signal_seen = 1;
    if (opt_stream_json) {
        json_prefix(log_id, qts, "NR", "serving_cell_info");
        printf(",\"version\":\"%u.%u\",\"pci\":%u,\"dl_nr_arfcn\":%u,\"ul_nr_arfcn\":%u,\"band\":%u}\n",
               rel_maj, rel_min, pci, dl, ul, band);
    }
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
        uint8_t cc_id = sub_ver == 4 ? cell_id : 0;
        lte_mac_dl_t *m = &state.mac_dl[cc_slot(cc_id)];
        memset(m, 0, sizeof(*m));
        m->valid = 1;
        m->version = sub_ver;
        m->cc_id = cc_id;
        m->sfn = sfn_subfn >> 4;
        m->subframe = sfn_subfn & 0xf;
        m->rnti_type = rnti_type;
        m->harq_id = harq_id;
        m->tbs_bytes = dl_tbs;
        m->padding_bytes = padding;
        m->num_sdu = rlc_pdus;
        m->num_lcid = 0;
        m->qts = qts;
        m->updated_at = time(NULL);
        sample_mac_seen = 1;
        if (opt_stream_json) {
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
        }
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
        uint8_t cc_id = sub_ver != 1 ? cell_id : 0;
        lte_mac_ul_t *m = &state.mac_ul[cc_slot(cc_id)];
        memset(m, 0, sizeof(*m));
        m->valid = 1;
        m->version = sub_ver;
        m->cc_id = cc_id;
        m->sfn = sfn_subfn >> 4;
        m->subframe = sfn_subfn & 0xf;
        m->rnti_type = rnti_type;
        m->harq_id = harq_id;
        m->grant = grant;
        m->rlc_pdus = rlc_pdus;
        m->padding_bytes = padding;
        m->bsr_event = bsr_event;
        m->bsr_trigger = bsr_trig;
        m->qts = qts;
        m->updated_at = time(NULL);
        sample_mac_seen = 1;
        if (opt_stream_json) {
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
        }
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
        uint8_t cc_id = bitu32(cc_harq, 0, 4);
        lte_mac_dl_t *m = &state.mac_dl[cc_slot(cc_id)];
        memset(m, 0, sizeof(*m));
        m->valid = 1;
        m->version = ver;
        m->cc_id = cc_id;
        m->sfn = bitu32(sfn_word, 0, 10);
        m->subframe = bitu32(sfn_word, 10, 4);
        m->rnti_type = bitu32(sfn_word, 15, 4);
        m->harq_id = bitu32(cc_harq, 4, 4);
        m->tbs_bytes = size;
        m->padding_bytes = pad;
        m->num_sdu = num_sdu;
        m->num_lcid = num_lcid;
        m->qts = qts;
        m->updated_at = time(NULL);
        sample_mac_seen = 1;
        if (opt_stream_json) {
            json_prefix(log_id, qts, "LTE", "lte_mac_transport_block");
            printf(",\"version\":%u,\"direction\":\"DL\",\"tb_index\":%u,"
                   "\"reason\":%u,\"num_lcid\":%u,\"sfn\":%u,\"subframe\":%u,"
                   "\"rnti_type\":%u,\"cc_id\":%u,\"harq_id\":%u,"
                   "\"tbs_bytes\":%u,\"padding_bytes\":%u,\"num_sdu\":%u,"
                   "\"mac_header_len\":%u}\n",
                   ver, i, reason, num_lcid, bitu32(sfn_word, 0, 10),
                   bitu32(sfn_word, 10, 4), bitu32(sfn_word, 15, 4),
                   cc_id, bitu32(cc_harq, 4, 4), size, pad,
                   num_sdu, bitu32(hdr_len_word, 0, 12));
        }
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
    if (len < 8) return 0;
    uint8_t ver = b[0];
    size_t rec_size;
    if (ver == 102) rec_size = 68;
    else if (ver == 144 || ver == 161) rec_size = 100;
    else return 0;
    if ((len - 8) % rec_size) return 0;
    uint16_t header_word = ver == 161 ? get16(b + 2) : get16(b + 1);
    uint16_t dispatch_tti = get16(b + 4);
    size_t count = (len - 8) / rec_size;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *rec = b + 8 + i * rec_size;
        uint16_t tti = get16(rec);
        uint16_t flags = get16(rec + 2);
        uint32_t alloc = get32(rec + 4);
        uint16_t tb_size = get16(rec + 8);
        uint16_t coding_rate_raw = get16(rec + 10);
        size_t mod_offset = rec_size == 68 ? 20 : 36;
        size_t tx_offset = rec_size == 68 ? 24 : 40;
        uint32_t mod_gain = get32(rec + mod_offset);
        uint32_t tx_cqi = get32(rec + tx_offset);
        state.pusch.valid = 1;
        state.pusch.version = ver;
        state.pusch.header_word = header_word;
        state.pusch.serving_cell_id = ver == 161 ? 0 : bitu32(header_word, 0, 4);
        state.pusch.dispatch_sfn = (dispatch_tti / 10) % 1024;
        state.pusch.dispatch_subframe = dispatch_tti % 10;
        state.pusch.record_index = (uint16_t)i;
        state.pusch.record_count = (uint16_t)count;
        state.pusch.tti = tti;
        state.pusch.sfn = (tti / 10) % 1024;
        state.pusch.subframe = tti % 10;
        state.pusch.carrier_id = bitu32(flags, 0, 2);
        state.pusch.ack_flag = bitu32(flags, 2, 1);
        state.pusch.cqi_flag = bitu32(flags, 3, 1);
        state.pusch.ri_flag = bitu32(flags, 4, 1);
        state.pusch.frequency_hopping = bitu32(flags, 5, 2);
        state.pusch.retx_index = bitu32(flags, 7, 5);
        state.pusch.rv = bitu32(flags, 12, 2);
        state.pusch.mirror_hopping = bitu32(flags, 14, 2);
        state.pusch.ra_type = bitu32(alloc, 0, 1);
        state.pusch.rb_start_slot0 = bitu32(alloc, 1, 7);
        state.pusch.rb_start_slot1 = bitu32(alloc, 8, 7);
        state.pusch.rb_count = bitu32(alloc, 15, 7);
        state.pusch.tb_size = tb_size;
        state.pusch.grant = tb_size;
        state.pusch.coding_rate_raw = coding_rate_raw;
        state.pusch.coding_rate = (double)coding_rate_raw / 1024.0;
        state.pusch.pusch_mod_order = bitu32(mod_gain, 0, 3);
        state.pusch.mod_gain_cluster_raw = mod_gain;
        state.pusch.tx_cqi_raw = tx_cqi;
        state.pusch.tx_power_raw = bitu32(tx_cqi, 0, 7);
        state.pusch.tx_power_dbm_candidate = (int16_t)state.pusch.tx_power_raw - 128;
        state.pusch.qts = qts;
        state.pusch.updated_at = time(NULL);
        if (opt_stream_json) {
            json_prefix(log_id, qts, "LTE", "lte_phy_pusch_tx_candidate");
            printf(",\"version\":%u,\"header_word\":%u,"
                   "\"dispatch_sfn\":%u,\"dispatch_subframe\":%u,"
                   "\"record_index\":%zu,\"record_count\":%zu,\"tti\":%u,"
                   "\"sfn_guess\":%u,\"subframe_guess\":%u,"
                   "\"record_flags_raw\":%u,\"carrier_id\":%u,"
                   "\"ack_flag\":%u,\"cqi_flag\":%u,\"ri_flag\":%u,"
                   "\"retx_index\":%u,\"rv\":%u,\"ra_type\":%u,"
                   "\"rb_start_slot0\":%u,\"rb_start_slot1\":%u,"
                   "\"rb_count\":%u,\"tb_size\":%u,\"grant\":%u,"
                   "\"coding_rate_raw\":%u,\"coding_rate\":%.3f,"
                   "\"pusch_mod_order\":%u,\"pusch_modulation\":",
                   ver, header_word, (dispatch_tti / 10) % 1024,
                   dispatch_tti % 10, i, count, tti, (tti / 10) % 1024,
                   tti % 10, flags, state.pusch.carrier_id,
                   state.pusch.ack_flag, state.pusch.cqi_flag,
                   state.pusch.ri_flag, state.pusch.retx_index,
                   state.pusch.rv, state.pusch.ra_type,
                   state.pusch.rb_start_slot0, state.pusch.rb_start_slot1,
                   state.pusch.rb_count, tb_size, tb_size,
                   coding_rate_raw, (double)coding_rate_raw / 1024.0,
                   state.pusch.pusch_mod_order);
            json_string(stdout, lte_modulation_name(state.pusch.pusch_mod_order));
            printf(",\"mod_gain_cluster_raw\":%u,\"tx_cqi_raw\":%u,"
                   "\"tx_power_raw\":%u,\"tx_power_dbm_candidate\":%d,"
                   "\"record_hex\":\"",
                   mod_gain, tx_cqi, state.pusch.tx_power_raw,
                   state.pusch.tx_power_dbm_candidate);
            hexprint(stdout, rec, rec_size);
            printf("\"}\n");
        }
    }
    return 1;
}

static int parse_lte_phy_pdsch_stat_candidate(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 44 || ((len - 4) % 40) != 0) return 0;
    uint16_t header_word = get16(b + 2);
    size_t count = (len - 4) / 40;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *rec = b + 4 + i * 40;
        memset(&state.pdsch, 0, sizeof(state.pdsch));
        state.pdsch.valid = 1;
        state.pdsch.version = b[0];
        state.pdsch.subversion = b[1];
        state.pdsch.header_word = header_word;
        state.pdsch.record_index = (uint16_t)i;
        state.pdsch.record_count = (uint16_t)count;
        state.pdsch.mcs_candidate_raw = rec[16];
        state.pdsch.field_0_raw = get16(rec);
        state.pdsch.field_2_raw = get16(rec + 2);
        state.pdsch.field_4_raw = get16(rec + 4);
        state.pdsch.field_8_raw = get16(rec + 8);
        state.pdsch.field_12_raw = get16(rec + 12);
        state.pdsch.field_16_raw = get16(rec + 16);
        state.pdsch.field_18_raw = get16(rec + 18);
        state.pdsch.field_20_raw = get16(rec + 20);
        state.pdsch.field_24_raw = get16(rec + 24);
        state.pdsch.field_28_raw = get16(rec + 28);
        state.pdsch.qts = qts;
        state.pdsch.updated_at = time(NULL);
        if (b[0] == 36) {
            uint16_t sfn_subframe = get16(rec);
            state.pdsch.decoded = 1;
            state.pdsch.subframe = bitu32(sfn_subframe, 0, 4);
            state.pdsch.sfn = bitu32(sfn_subframe, 4, 12);
            state.pdsch.num_rbs = rec[2];
            state.pdsch.num_layers = rec[3];
            state.pdsch.num_transport_blocks = rec[4];
            state.pdsch.serving_cell_id = bitu32(rec[5], 0, 3);
            state.pdsch.hsic_enabled = bitu32(rec[5], 3, 4);
            uint8_t tb_limit = state.pdsch.num_transport_blocks;
            if (tb_limit > 2) tb_limit = 2;
            for (uint8_t tb_i = 0; tb_i < tb_limit; tb_i++) {
                const uint8_t *tbp = rec + 12 + (size_t)tb_i * 12;
                lte_phy_pdsch_tb_t *tb = &state.pdsch.tb[tb_i];
                tb->valid = 1;
                tb->harq_id = bitu32(tbp[0], 0, 4);
                tb->rv = bitu32(tbp[0], 4, 2);
                tb->ndi = bitu32(tbp[0], 6, 1);
                tb->crc_result = bitu32(tbp[0], 7, 1);
                tb->rnti_type = bitu32(tbp[1], 0, 4);
                tb->tb_index = bitu32(tbp[1], 4, 1);
                tb->discarded_retx_present = bitu32(tbp[1], 5, 1);
                tb->did_recombining = bitu32(tbp[1], 6, 1);
                tb->tb_size = get16(tbp + 4);
                tb->mcs = tbp[6];
                tb->num_rbs = tbp[7];
                tb->modulation_type = tbp[8];
                tb->qed2_interim = tbp[9];
                if (tb->crc_result) lte_pdsch_crc_pass_total++;
                else lte_pdsch_crc_fail_total++;
            }
            unsigned long long total = lte_pdsch_crc_pass_total + lte_pdsch_crc_fail_total;
            state.pdsch.crc_pass_total = lte_pdsch_crc_pass_total;
            state.pdsch.crc_fail_total = lte_pdsch_crc_fail_total;
            state.pdsch.dl_bler = total ? (double)lte_pdsch_crc_fail_total / (double)total : 0.0;
        }
        if (opt_stream_json && state.pdsch.decoded) {
            json_prefix(log_id, qts, "LTE", "lte_phy_pdsch_stat_candidate");
            printf(",\"confidence\":\"layout_v36_decoded\","
                   "\"version\":%u,\"header_word\":%u,"
                   "\"record_index\":%zu,\"record_count\":%zu,"
                   "\"sfn\":%u,\"subframe\":%u,\"num_rbs\":%u,"
                   "\"num_layers\":%u,\"num_transport_blocks\":%u,"
                   "\"serving_cell_id\":%u,\"hsic_enabled\":%u,"
                   "\"crc_pass_total\":%llu,\"crc_fail_total\":%llu,"
                   "\"dl_bler\":%.4f,\"transport_blocks\":[",
                   b[0], header_word, i, count, state.pdsch.sfn,
                   state.pdsch.subframe, state.pdsch.num_rbs,
                   state.pdsch.num_layers, state.pdsch.num_transport_blocks,
                   state.pdsch.serving_cell_id, state.pdsch.hsic_enabled,
                   state.pdsch.crc_pass_total, state.pdsch.crc_fail_total,
                   state.pdsch.dl_bler);
            int first_tb = 1;
            for (int tb_i = 0; tb_i < 2; tb_i++) {
                const lte_phy_pdsch_tb_t *tb = &state.pdsch.tb[tb_i];
                if (!tb->valid) continue;
                if (!first_tb) fputc(',', stdout);
                printf("{\"tb_index\":%u,\"harq_id\":%u,\"rv\":%u,"
                       "\"ndi\":%u,\"crc_result\":%u,\"rnti_type\":%u,"
                       "\"tb_size\":%u,\"mcs\":%u,\"num_rbs\":%u,"
                       "\"modulation_type\":%u,\"modulation\":",
                       tb->tb_index, tb->harq_id, tb->rv, tb->ndi,
                       tb->crc_result, tb->rnti_type, tb->tb_size,
                       tb->mcs, tb->num_rbs, tb->modulation_type);
                json_string(stdout, lte_modulation_name(tb->modulation_type));
                printf("}");
                first_tb = 0;
            }
            printf("],\"record_hex\":\"");
            hexprint(stdout, rec, 40);
            printf("\"}\n");
        } else if (opt_stream_json) {
            json_prefix(log_id, qts, "LTE", "lte_phy_pdsch_stat_candidate");
            printf(",\"confidence\":\"candidate_unconfirmed\","
                   "\"version\":%u,\"subversion\":%u,\"header_word\":%u,"
                   "\"record_index\":%zu,\"record_count\":%zu,"
                   "\"tti_guess\":%u,\"mcs_candidate_raw\":%u,"
                   "\"field_0_raw\":%u,\"field_2_raw\":%u,"
                   "\"field_4_raw\":%u,\"field_8_raw\":%u,"
                   "\"field_12_raw\":%u,\"field_16_raw\":%u,"
                   "\"field_18_raw\":%u,\"field_20_raw\":%u,"
                   "\"field_24_raw\":%u,\"field_28_raw\":%u,"
                   "\"record_hex\":\"",
                   b[0], b[1], header_word, i, count, get16(rec), rec[16],
                   get16(rec), get16(rec + 2), get16(rec + 4),
                   get16(rec + 8), get16(rec + 12), get16(rec + 16),
                   get16(rec + 18), get16(rec + 20), get16(rec + 24),
                   get16(rec + 28));
            hexprint(stdout, rec, 40);
            printf("\"}\n");
        }
    }
    return 1;
}

static int parse_nr_ml1_meas_database_update(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    (void)log_id;
    if (len < 6) return 0;
    uint16_t rel_min = get16(b);
    uint16_t rel_maj = get16(b + 2);
    uint8_t num_layers = 0, ssb_periodicity = 0;
    size_t pos = 0;
    if (rel_maj == 2 && rel_min == 7) {
        if (len < 16) return 0;
        num_layers = b[4];
        ssb_periodicity = b[5];
        pos = 16;
    } else if ((rel_maj == 2 && (rel_min == 9 || rel_min == 10)) ||
               (rel_maj == 3 && rel_min == 0)) {
        if (len < 20) return 0;
        num_layers = b[8];
        ssb_periodicity = b[9];
        pos = 20;
    } else {
        return 0;
    }

    memset(state.nr_layers, 0, sizeof(state.nr_layers));
    snprintf(state.nr_ml1_version, sizeof(state.nr_ml1_version), "%u.%u", rel_maj, rel_min);
    state.nr_layer_count = num_layers;
    state.nr_ssb_periodicity = ssb_periodicity;
    state.nr_ml1_qts = qts;
    state.nr_ml1_updated_at = time(NULL);

    for (uint8_t layer_index = 0; layer_index < num_layers; layer_index++) {
        nr_layer_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.valid = layer_index < MAX_NR_LAYERS;
        tmp.layer = layer_index;
        tmp.rx_beam[0] = -1;
        tmp.rx_beam[1] = -1;
        tmp.qts = qts;
        tmp.updated_at = state.nr_ml1_updated_at;

        if (rel_maj == 2) {
            if (pos + 32 > len) break;
            tmp.nr_arfcn = get32(b + pos);
            tmp.num_cells = b[pos + 4];
            tmp.serving_cell_index = b[pos + 5];
            tmp.serving_pci = get16(b + pos + 6);
            tmp.serving_ssb = b[pos + 8] & 0x0f;
            tmp.serving_rsrp_count = 2;
            tmp.serving_rsrp_dbm[0] = q7_signed(get32(b + pos + 12));
            tmp.serving_rsrp_dbm[1] = q7_signed(get32(b + pos + 16));
            uint16_t rx0 = get16(b + pos + 20);
            uint16_t rx1 = get16(b + pos + 22);
            tmp.rx_beam[0] = rx0 == 0xffff ? -1 : rx0;
            tmp.rx_beam[1] = rx1 == 0xffff ? -1 : rx1;
            tmp.rfic_id = get16(b + pos + 24);
            tmp.subarray[0] = get16(b + pos + 28);
            tmp.subarray[1] = get16(b + pos + 30);
            pos += 32;
        } else {
            if (pos + 40 > len) break;
            tmp.nr_arfcn = get32(b + pos);
            tmp.has_cc_id = 1;
            tmp.cc_id = b[pos + 4];
            tmp.num_cells = b[pos + 5];
            tmp.serving_pci = get16(b + pos + 6);
            tmp.serving_cell_index = b[pos + 8];
            tmp.serving_ssb = b[pos + 9] & 0x0f;
            tmp.serving_rsrp_count = 4;
            tmp.serving_rsrp_dbm[0] = q7_signed(get32(b + pos + 12));
            tmp.serving_rsrp_dbm[1] = q7_signed(get32(b + pos + 16));
            tmp.serving_rsrp_dbm[2] = q7_signed(get32(b + pos + 20));
            tmp.serving_rsrp_dbm[3] = q7_signed(get32(b + pos + 24));
            uint16_t rx0 = get16(b + pos + 28);
            uint16_t rx1 = get16(b + pos + 30);
            tmp.rx_beam[0] = rx0 == 0xffff ? -1 : rx0;
            tmp.rx_beam[1] = rx1 == 0xffff ? -1 : rx1;
            tmp.rfic_id = get16(b + pos + 32);
            tmp.subarray[0] = get16(b + pos + 36);
            tmp.subarray[1] = get16(b + pos + 38);
            pos += 40;
        }

        uint8_t n_cells = tmp.num_cells;
        if (n_cells == 0 || n_cells == 0xff) {
            n_cells = (tmp.serving_cell_index > 0 && tmp.serving_cell_index < 0xff) ? tmp.serving_cell_index : 0;
        }
        for (uint8_t cell_index = 0; cell_index < n_cells; cell_index++) {
            if (pos + 16 > len) break;
            uint16_t pci = get16(b + pos);
            uint16_t pbch_sfn = get16(b + pos + 2);
            uint8_t num_beams = b[pos + 4];
            double cell_rsrp = q7_signed(get32(b + pos + 8));
            double cell_rsrq = q7_signed(get32(b + pos + 12));
            nr_cell_t *cell = NULL;
            if (tmp.valid && cell_index < MAX_NR_CELLS) {
                cell = &tmp.cells[cell_index];
                cell->valid = 1;
                cell->index = cell_index;
                cell->pci = pci;
                cell->pbch_sfn = pbch_sfn;
                cell->num_beams = num_beams;
                cell->rsrp_dbm = cell_rsrp;
                cell->rsrq_db = cell_rsrq;
                tmp.cells_count++;
            }
            pos += 16;

            size_t beam_size = (rel_maj == 2 && (rel_min == 7 || rel_min == 9)) ? 44 : 84;
            for (uint8_t beam_index = 0; beam_index < num_beams; beam_index++) {
                if (pos + beam_size > len) {
                    pos = len;
                    break;
                }
                if (cell && beam_index < MAX_NR_BEAMS) {
                    nr_beam_t *beam = &cell->beams[beam_index];
                    beam->valid = 1;
                    beam->index = beam_index;
                    beam->ssb_index = get16(b + pos);
                    uint16_t brx0 = get16(b + pos + 4);
                    uint16_t brx1 = get16(b + pos + 6);
                    beam->rx_beam[0] = brx0 == 0xffff ? -1 : brx0;
                    beam->rx_beam[1] = brx1 == 0xffff ? -1 : brx1;
                    beam->ssb_ref_timing = get64(b + pos + 12);
                    beam->rsrp_dbm[0] = q7_signed(get32(b + pos + 20));
                    beam->rsrp_dbm[1] = q7_signed(get32(b + pos + 24));
                    if (beam_size == 44) {
                        beam->filtered_nr2nr_rsrp_dbm = q7_signed(get32(b + pos + 28));
                        beam->filtered_nr2nr_rsrq_db = q7_signed(get32(b + pos + 32));
                        beam->filtered_l2nr_rsrp_dbm = q7_signed(get32(b + pos + 36));
                        beam->filtered_l2nr_rsrq_db = q7_signed(get32(b + pos + 40));
                    } else {
                        beam->has_rsrq = 1;
                        beam->rsrq_db[0] = q7_signed(get32(b + pos + 28));
                        beam->rsrq_db[1] = q7_signed(get32(b + pos + 32));
                        beam->filtered_nr2nr_rsrp_dbm = q7_signed(get32(b + pos + 68));
                        beam->filtered_nr2nr_rsrq_db = q7_signed(get32(b + pos + 72));
                        beam->filtered_l2nr_rsrp_dbm = q7_signed(get32(b + pos + 76));
                        beam->filtered_l2nr_rsrq_db = q7_signed(get32(b + pos + 80));
                    }
                }
                pos += beam_size;
            }
        }
        if (tmp.valid) state.nr_layers[layer_index] = tmp;
    }

    sample_signal_seen = 1;
    sample_nr_seen = 1;
    return 1;
}

static int parse_nr_mac_ul_tb_stats_candidate(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 20) return 0;
    uint16_t minor = get16(b);
    uint16_t major = get16(b + 2);
    uint8_t count = b[15];
    size_t rec_size;
    if (major == 2 && minor == 0) rec_size = 92;
    else if (major == 2 && minor == 1) rec_size = 96;
    else return 0;
    if (!count) count = (uint8_t)((len - 20) / rec_size);
    if (len < 20 + (size_t)count * rec_size) return 0;

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t *rec = b + 20 + (size_t)i * rec_size;
        nr_mac_ul_tb_stats_t *s = &state.nr_ul_tb_stats;
        memset(s, 0, sizeof(*s));
        s->valid = 1;
        s->major = major;
        s->minor = minor;
        s->record_index = i;
        s->record_count = count;
        s->tb_new_tx_bytes = get64(rec);
        s->tb_retx_bytes = get64(rec + 8);
        s->num_mcs = get64(rec + 16);
        s->num_prb = get64(rec + 24);
        s->phr = get64(rec + 32);
        if (minor == 0) {
            s->total_power = get32(rec + 40);
            s->num_new_tx_tb = get32(rec + 44);
            s->num_retx_tb = get32(rec + 48);
            s->num_dtx = get32(rec + 52);
            s->num_ri = get32(rec + 56);
            s->ri = get32(rec + 60);
            s->num_cqi = get32(rec + 64);
            s->cqi = get32(rec + 68);
            s->num_phr = get32(rec + 72);
            s->tpc_accum = get32(rec + 76);
            s->num_ulsch_sched = get32(rec + 80);
            s->num_no_ulsch_sched = get32(rec + 84);
            s->pcmax_raw = get16(rec + 88);
            s->flush_gap_count = get16(rec + 90);
        } else {
            s->total_power = get64(rec + 40);
            s->num_new_tx_tb = get32(rec + 48);
            s->num_retx_tb = get32(rec + 52);
            s->num_ri = get32(rec + 56);
            s->ri = get32(rec + 60);
            s->num_cqi = get32(rec + 64);
            s->cqi = get32(rec + 68);
            s->num_phr = get32(rec + 72);
            s->tpc_accum = get32(rec + 76);
            s->num_ulsch_sched = get32(rec + 80);
            s->num_no_ulsch_sched = get32(rec + 84);
            s->pcmax_raw = get16(rec + 88);
            s->flush_gap_count = get16(rec + 90);
        }
        double tb_total = (double)s->num_new_tx_tb + (double)s->num_retx_tb;
        double sched_total = (double)s->num_ulsch_sched + (double)s->num_no_ulsch_sched;
        double mcs_den = tb_total > 0.0 ? tb_total : (double)s->num_ulsch_sched;
        s->avg_mcs_candidate = div0((double)s->num_mcs, mcs_den);
        s->avg_prb_candidate = div0((double)s->num_prb, mcs_den);
        s->avg_phr_candidate = div0((double)s->phr, (double)s->num_phr);
        s->avg_ri_candidate = div0((double)s->ri, (double)s->num_ri);
        s->avg_cqi_candidate = div0((double)s->cqi, (double)s->num_cqi);
        s->retx_tb_ratio = div0((double)s->num_retx_tb, tb_total);
        s->ulsch_sched_ratio = div0((double)s->num_ulsch_sched, sched_total);
        s->pcmax_dbm_candidate = (double)s->pcmax_raw / 10.0;
        s->qts = qts;
        s->updated_at = time(NULL);
        sample_mac_seen = 1;
        sample_nr_seen = 1;

        if (opt_stream_json) {
            json_prefix(log_id, qts, "NR", "nr_mac_ul_tb_stats");
            printf(",\"confidence\":\"layout_v%u_%u_decoded\","
                   "\"version\":\"%u.%u\",\"record_index\":%u,"
                   "\"record_count\":%u,\"tb_new_tx_bytes\":%llu,"
                   "\"tb_retx_bytes\":%llu,\"num_mcs\":%llu,"
                   "\"avg_mcs_candidate\":%.2f,\"num_prb\":%llu,"
                   "\"avg_prb_candidate\":%.2f,\"phr\":%llu,"
                   "\"avg_phr_candidate\":%.2f,\"total_power\":%llu,"
                   "\"num_new_tx_tb\":%u,\"num_retx_tb\":%u,"
                   "\"retx_tb_ratio\":%.4f,\"num_dtx\":%u,"
                   "\"num_ri\":%u,\"ri\":%u,\"avg_ri_candidate\":%.2f,"
                   "\"num_cqi\":%u,\"cqi\":%u,\"avg_cqi_candidate\":%.2f,"
                   "\"num_phr\":%u,\"tpc_accum\":%u,"
                   "\"num_ulsch_sched\":%u,\"num_no_ulsch_sched\":%u,"
                   "\"ulsch_sched_ratio\":%.4f,\"pcmax_raw\":%u,"
                   "\"pcmax_dbm_candidate\":%.1f,\"flush_gap_count\":%u}\n",
                   major, minor, major, minor, i, count,
                   (unsigned long long)s->tb_new_tx_bytes,
                   (unsigned long long)s->tb_retx_bytes,
                   (unsigned long long)s->num_mcs, s->avg_mcs_candidate,
                   (unsigned long long)s->num_prb, s->avg_prb_candidate,
                   (unsigned long long)s->phr, s->avg_phr_candidate,
                   (unsigned long long)s->total_power, s->num_new_tx_tb,
                   s->num_retx_tb, s->retx_tb_ratio, s->num_dtx,
                   s->num_ri, s->ri, s->avg_ri_candidate, s->num_cqi,
                   s->cqi, s->avg_cqi_candidate, s->num_phr, s->tpc_accum,
                   s->num_ulsch_sched, s->num_no_ulsch_sched,
                   s->ulsch_sched_ratio, s->pcmax_raw,
                   s->pcmax_dbm_candidate, s->flush_gap_count);
        }
    }
    return 1;
}

static int parse_nr_mac_pdsch_stats_candidate(uint16_t log_id, uint64_t qts, const uint8_t *b, size_t len) {
    if (len < 28) return 0;
    uint16_t minor = get16(b);
    uint16_t major = get16(b + 2);
    if (!(major == 2 && minor == 2)) return 0;
    uint8_t count = b[15];
    const size_t rec_size = 72;
    if (!count) count = (uint8_t)((len - 28) / rec_size);
    if (len < 28 + (size_t)count * rec_size) return 0;

    for (uint8_t i = 0; i < count; i++) {
        const uint8_t *rec = b + 28 + (size_t)i * rec_size;
        uint32_t carrier_id = get32(rec);
        nr_mac_pdsch_stats_t *s = &state.nr_pdsch_stats[cc_slot((uint8_t)carrier_id)];
        memset(s, 0, sizeof(*s));
        s->valid = 1;
        s->major = major;
        s->minor = minor;
        s->record_index = i;
        s->record_count = count;
        s->carrier_id = carrier_id;
        s->num_slots_elapsed = get32(rec + 4);
        s->num_pdsch_decode = get32(rec + 8);
        s->num_crc_pass_tb = get32(rec + 12);
        s->num_crc_fail_tb = get32(rec + 16);
        s->num_retx = get32(rec + 20);
        s->ack_as_nack = get32(rec + 24);
        s->harq_failure = get32(rec + 28);
        s->crc_pass_tb_bytes = get64(rec + 32);
        s->crc_fail_tb_bytes = get64(rec + 40);
        s->tb_bytes = get64(rec + 48);
        s->padding_bytes = get64(rec + 56);
        s->retx_bytes = get64(rec + 64);
        double crc_total = (double)s->num_crc_pass_tb + (double)s->num_crc_fail_tb;
        double byte_total = (double)s->crc_pass_tb_bytes + (double)s->crc_fail_tb_bytes;
        s->dl_bler = div0((double)s->num_crc_fail_tb, crc_total);
        s->byte_error_ratio = div0((double)s->crc_fail_tb_bytes, byte_total);
        s->retx_ratio = div0((double)s->num_retx, (double)s->num_pdsch_decode);
        s->qts = qts;
        s->updated_at = time(NULL);
        sample_mac_seen = 1;
        sample_nr_seen = 1;

        if (opt_stream_json) {
            json_prefix(log_id, qts, "NR", "nr_mac_pdsch_stats");
            printf(",\"confidence\":\"layout_v%u_%u_decoded\","
                   "\"version\":\"%u.%u\",\"record_index\":%u,"
                   "\"record_count\":%u,\"carrier_id\":%u,"
                   "\"num_slots_elapsed\":%u,\"num_pdsch_decode\":%u,"
                   "\"num_crc_pass_tb\":%u,\"num_crc_fail_tb\":%u,"
                   "\"dl_bler\":%.4f,\"num_retx\":%u,"
                   "\"retx_ratio\":%.4f,\"ack_as_nack\":%u,"
                   "\"harq_failure\":%u,\"crc_pass_tb_bytes\":%llu,"
                   "\"crc_fail_tb_bytes\":%llu,\"byte_error_ratio\":%.4f,"
                   "\"tb_bytes\":%llu,\"padding_bytes\":%llu,"
                   "\"retx_bytes\":%llu}\n",
                   major, minor, major, minor, i, count, s->carrier_id,
                   s->num_slots_elapsed, s->num_pdsch_decode,
                   s->num_crc_pass_tb, s->num_crc_fail_tb, s->dl_bler,
                   s->num_retx, s->retx_ratio, s->ack_as_nack,
                   s->harq_failure, (unsigned long long)s->crc_pass_tb_bytes,
                   (unsigned long long)s->crc_fail_tb_bytes,
                   s->byte_error_ratio, (unsigned long long)s->tb_bytes,
                   (unsigned long long)s->padding_bytes,
                   (unsigned long long)s->retx_bytes);
        }
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
    else if (log_id == LOG_LTE_PHY_PDSCH_STAT_INDICATION2) parsed = parse_lte_phy_pdsch_stat_candidate(log_id, qts, body, body_len);
    else if (log_id == LOG_NR_ML1) parsed = parse_nr_ml1_meas_database_update(log_id, qts, body, body_len);
    else if (log_id == LOG_NR_MAC_UL_TB_STATS) parsed = parse_nr_mac_ul_tb_stats_candidate(log_id, qts, body, body_len);
    else if (log_id == LOG_NR_MAC_PDSCH_STATS) parsed = parse_nr_mac_pdsch_stats_candidate(log_id, qts, body, body_len);
    else if (log_id == LOG_LTE_CA) {
        got_lte = 1;
        write_combo(combo_dir, "b0cd_qlte.hex", body, body_len);
        state.lte_combo.valid = 1;
        snprintf(state.lte_combo.format, sizeof(state.lte_combo.format), "%s", "QLTE");
        combo_path(state.lte_combo.path, sizeof(state.lte_combo.path), "b0cd_qlte.hex");
        state.lte_combo.bytes = body_len;
        state.lte_combo.qts = qts;
        state.lte_combo.updated_at = time(NULL);
        if (opt_stream_json) {
            json_prefix(log_id, qts, "LTE", "supported_ca_combos_raw");
            printf(",\"format\":\"QLTE\",\"payload_hex\":\""); hexprint(stdout, body, body_len); printf("\"}\n");
        }
        parsed = 1;
    } else if (log_id == LOG_NR_CA) {
        got_nr = 1;
        write_combo(combo_dir, "b826_qnr.hex", body, body_len);
        state.nr_combo.valid = 1;
        snprintf(state.nr_combo.format, sizeof(state.nr_combo.format), "%s", "QNR");
        combo_path(state.nr_combo.path, sizeof(state.nr_combo.path), "b826_qnr.hex");
        state.nr_combo.bytes = body_len;
        state.nr_combo.qts = qts;
        state.nr_combo.updated_at = time(NULL);
        if (opt_stream_json) {
            json_prefix(log_id, qts, "NR", "supported_ca_combos_raw");
            printf(",\"format\":\"QNR\",\"payload_hex\":\""); hexprint(stdout, body, body_len); printf("\"}\n");
        }
        parsed = 1;
    }
    if (!opt_no_raw_log && opt_stream_json &&
        ((!parsed && opt_debug) || opt_raw ||
        (opt_probe_scheduling && is_probe_scheduling_log(log_id)) ||
        (opt_probe_phy && is_probe_phy_log(log_id)))) {
        json_prefix(log_id, qts, "RAW", "diag_log_raw");
        printf(",\"body_len\":%zu,\"body_hex\":\"", body_len);
        hexprint(stdout, body, body_len);
        printf("\"}\n");
    }
    if (opt_stream_json) fflush(stdout);
}

static void on_log(unsigned char *ptr, int len) {
    if (!ptr || len < 12) return;
    events_seen++;
    sample_events_seen++;
    uint16_t rec_len = get16(ptr);
    uint16_t log_id = get16(ptr + 2);
    uint64_t qts = get64(ptr + 4);
    if (rec_len > (uint16_t)len) rec_len = (uint16_t)len;
    if (rec_len < 12) return;
    track_log(log_id, qts);
    parse_log_body(log_id, qts, ptr + 12, rec_len - 12);
}

static void on_event(unsigned char *ptr, int len) {
    if (!opt_debug || !opt_stream_json || opt_no_raw_log) return;
    printf("{\"event\":\"diag_event_raw\",\"len\":%d,\"payload_hex\":\"", len);
    hexprint(stdout, ptr, len > 256 ? 256 : (size_t)len);
    printf("\"}\n");
    fflush(stdout);
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [--seconds N] [--proc msm|mdm|auto] [--debug] [--raw]\n"
        "          [--mac] [--probe-scheduling] [--probe-phy] [--combo-dir DIR]\n"
        "          [--gui-lite] [--full-logset] [--oneshot]\n"
        "          [--require any|signal|signal-mac|mac|nr] [--sample-min-ms N]\n"
        "          [--snapshot-file PATH] [--snapshot-interval-ms N]\n"
        "          [--sample-window-ms N] [--nice N]\n"
        "          [--stale-after-ms N] [--no-raw-log] [--max-runtime-sec N]\n"
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
        {"snapshot-file", required_argument, 0, 8},
        {"snapshot-interval-ms", required_argument, 0, 9},
        {"no-raw-log", no_argument, 0, 10},
        {"max-runtime-sec", required_argument, 0, 11},
        {"stale-after-ms", required_argument, 0, 12},
        {"sample-window-ms", required_argument, 0, 13},
        {"nice", required_argument, 0, 14},
        {"oneshot", no_argument, 0, 15},
        {"require", required_argument, 0, 16},
        {"sample-min-ms", required_argument, 0, 17},
        {"gui-lite", no_argument, 0, 18},
        {"full-logset", no_argument, 0, 19},
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
        else if (c == 8) snapshot_file = optarg;
        else if (c == 9) snapshot_interval_ms = atoi(optarg);
        else if (c == 10) opt_no_raw_log = 1;
        else if (c == 11) seconds = atoi(optarg);
        else if (c == 12) stale_after_ms = atoi(optarg);
        else if (c == 13) sample_window_ms = atoi(optarg);
        else if (c == 14) nice_increment = atoi(optarg);
        else if (c == 15) opt_oneshot = 1;
        else if (c == 16) {
            if (!strcmp(optarg, "any")) require_mode = REQUIRE_ANY;
            else if (!strcmp(optarg, "mac")) require_mode = REQUIRE_MAC;
            else if (!strcmp(optarg, "nr")) require_mode = REQUIRE_NR;
            else if (!strcmp(optarg, "signal-mac")) require_mode = REQUIRE_SIGNAL_MAC;
            else if (!strcmp(optarg, "signal")) require_mode = REQUIRE_SIGNAL;
            else { usage(argv[0]); return 2; }
        }
        else if (c == 17) sample_min_ms = atoi(optarg);
        else if (c == 18) opt_gui_lite = 1;
        else if (c == 19) opt_full_logset = 1;
        else { usage(argv[0]); return c == 'h' ? 0 : 2; }
    }
    if (snapshot_interval_ms <= 0) snapshot_interval_ms = 10000;
    if (stale_after_ms <= 0) stale_after_ms = 30000;
    if (sample_min_ms < 0) sample_min_ms = 0;
    if (snapshot_file) {
        opt_stream_json = 0;
        opt_no_raw_log = 1;
        if (!opt_full_logset && !opt_probe_scheduling && !opt_probe_phy) opt_gui_lite = 1;
        if (opt_oneshot && seconds <= 0) seconds = stale_after_ms > 0 ? (stale_after_ms + 999) / 1000 : 10;
        if (sample_window_ms < 0) {
            if (opt_oneshot) sample_window_ms = seconds > 0 ? seconds * 1000 : snapshot_interval_ms;
            else sample_window_ms = 2000;
        }
        if (nice_increment == 0) nice_increment = 5;
    }
    if (sample_window_ms < 0) sample_window_ms = 0;
    if (sample_window_ms > snapshot_interval_ms) sample_window_ms = snapshot_interval_ms;
    runtime_start = time(NULL);
    if (nice_increment > 0) {
        errno = 0;
        int cur_prio = getpriority(PRIO_PROCESS, 0);
        if (errno == 0) {
            int new_prio = cur_prio + nice_increment;
            if (new_prio > 19) new_prio = 19;
            setpriority(PRIO_PROCESS, 0, new_prio);
        }
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
        snprintf(last_error, sizeof(last_error), "Diag_LSM_Init failed errno=%d", errno);
        write_snapshot(0);
        return 1;
    }

    int client_id = -1;
    diag_dci_peripherals list = DIAG_CON_MPSS;
    int err = diag_register_dci_client(&client_id, &list, proc, &dci_sig);
    if (err != DIAG_DCI_NO_ERROR) {
        fprintf(stderr, "diag_register_dci_client proc=%d failed err=%d errno=%d\n", proc, err, errno);
        snprintf(last_error, sizeof(last_error), "diag_register_dci_client failed err=%d errno=%d", err, errno);
        write_snapshot(0);
        Diag_LSM_DeInit();
        return 1;
    }
    err = diag_get_dci_support_list_proc(proc, &list);
    if (opt_debug && opt_stream_json && err == DIAG_DCI_NO_ERROR) {
        printf("{\"event\":\"dci_support\",\"proc\":%d,\"mpss\":%s,\"apss\":%s,\"lpass\":%s,\"wcnss\":%s}\n",
               proc, (list & DIAG_CON_MPSS) ? "true" : "false",
               (list & DIAG_CON_APSS) ? "true" : "false",
               (list & DIAG_CON_LPASS) ? "true" : "false",
               (list & DIAG_CON_WCNSS) ? "true" : "false");
    }
    err = diag_register_dci_signal_data(client_id, dci_sig);
    if (err != DIAG_DCI_NO_ERROR && err != DIAG_DCI_NOT_SUPPORTED) {
        fprintf(stderr, "diag_register_dci_signal_data failed err=%d errno=%d\n", err, errno);
        snprintf(last_error, sizeof(last_error), "diag_register_dci_signal_data failed err=%d errno=%d", err, errno);
    }
    err = diag_register_dci_stream_proc(client_id, on_log, on_event);
    if (err != DIAG_DCI_NO_ERROR) {
        fprintf(stderr, "diag_register_dci_stream_proc failed err=%d errno=%d\n", err, errno);
        snprintf(last_error, sizeof(last_error), "diag_register_dci_stream_proc failed err=%d errno=%d", err, errno);
    }
    diag_dci_vote_real_time(client_id, MODE_REALTIME);

    uint16 logs[64];
    int count = 0;
    if (opt_gui_lite) {
        logs[count++] = LOG_LTE_SMEAS;
        logs[count++] = LOG_LTE_SMEAS_RESP;
        logs[count++] = LOG_LTE_RRC_SCELL;
        logs[count++] = LOG_NR_RRC_SCELL;
        logs[count++] = LOG_NR_ML1;
    } else {
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
    }
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
        logs[count++] = LOG_LTE_PHY_SRS_TX_REPORT;
        logs[count++] = LOG_LTE_PHY_RACH_TX_REPORT;
        logs[count++] = LOG_LTE_PHY_PUCCH_CSF_REPORT;
        logs[count++] = LOG_LTE_PHY_PUSCH_CSF_REPORT;
        logs[count++] = LOG_LTE_PHY_PDCCH_PHICH_INDICATION;
        logs[count++] = LOG_LTE_PHY_GM_TX_REPORT;
        logs[count++] = LOG_LTE_PHY_PDSCH_STAT_INDICATION2;
        logs[count++] = LOG_LTE_PHY_PUSCH_STAT_INDICATION;
        logs[count++] = LOG_LTE_PHY_LEGACY_B175;
        logs[count++] = LOG_LTE_PHY_LEGACY_B176;
        logs[count++] = LOG_LTE_PHY_LEGACY_B177;
        logs[count++] = LOG_LTE_PHY_LEGACY_B178;
        logs[count++] = LOG_NR_MAC_UL_TB_STATS;
        logs[count++] = LOG_NR_MAC_UL_PHYS_CH_SCHED;
        logs[count++] = LOG_NR_MAC_PDSCH_STATS;
        logs[count++] = LOG_NR_ML1_SERVING_CELL_BEAM_MGMT;
    }
    err = set_log_stream_state(client_id, logs, count, 1, &log_stream_enabled);
    if (err != DIAG_DCI_NO_ERROR) {
        fprintf(stderr, "diag_log_stream_config enable failed err=%d errno=%d\n", err, errno);
    }

    time_t start = time(NULL);
    uint64_t loop_start_ms = monotonic_ms();
    uint64_t cycle_anchor_ms = loop_start_ms;
    uint64_t next_snapshot_ms = loop_start_ms + (uint64_t)snapshot_interval_ms;
    int exit_code = 0;
    if (snapshot_file) write_snapshot(1);
    while (running) {
        uint64_t now_ms = monotonic_ms();
        uint64_t sample_elapsed_ms = sample_started_ms ? now_ms - sample_started_ms : 0;
        int have_enough = sample_has_enough_data();
        if (snapshot_file && sample_window_ms > 0) {
            uint64_t elapsed = now_ms - cycle_anchor_ms;
            int new_cycle = 0;
            while (elapsed >= (uint64_t)snapshot_interval_ms) {
                cycle_anchor_ms += (uint64_t)snapshot_interval_ms;
                elapsed = now_ms - cycle_anchor_ms;
                new_cycle = 1;
            }
            if (new_cycle) {
                reset_sample_cycle();
                sample_elapsed_ms = 0;
                have_enough = 0;
            }
            int should_enable = elapsed < (uint64_t)sample_window_ms;
            if (should_enable && have_enough && sample_elapsed_ms >= (uint64_t)sample_min_ms) {
                should_enable = 0;
                if (log_stream_enabled && !opt_oneshot) write_snapshot(1);
            }
            set_log_stream_state(client_id, logs, count, should_enable, &log_stream_enabled);
        }
        if (opt_oneshot && have_enough && sample_elapsed_ms >= (uint64_t)sample_min_ms) break;
        if (opt_oneshot && sample_window_ms > 0 &&
            sample_started_ms && sample_elapsed_ms >= (uint64_t)sample_window_ms &&
            !have_enough) {
            snprintf(last_error, sizeof(last_error),
                     "sample timeout: require=%s not seen in %d ms",
                     require_mode_name(), sample_window_ms);
            exit_code = 2;
            break;
        }
        if (snapshot_file) {
            if (now_ms >= next_snapshot_ms) {
                write_snapshot(1);
                do {
                    next_snapshot_ms += (uint64_t)snapshot_interval_ms;
                } while (now_ms >= next_snapshot_ms);
            }
        }
        if (seconds > 0 && time(NULL) - start >= seconds) {
            if (opt_oneshot && !sample_has_enough_data()) {
                snprintf(last_error, sizeof(last_error),
                         "sample timeout: require=%s not seen in %d sec",
                         require_mode_name(), seconds);
                exit_code = 2;
            }
            break;
        }
        if (wait) {
            if ((!strcmp(wait, "lte") && got_lte) || (!strcmp(wait, "nr") && got_nr) ||
                (!strcmp(wait, "both") && got_lte && got_nr)) break;
        }
        sleep_ms(snapshot_file && sample_window_ms > 0 ? 100 : 500);
    }

    set_log_stream_state(client_id, logs, count, 0, &log_stream_enabled);
    diag_disable_all_logs(client_id);
    diag_deregister_dci_signal_data(client_id, dci_sig);
    diag_release_dci_client(&client_id);
    Diag_LSM_DeInit();
    if (snapshot_file) write_snapshot(0);
    return exit_code;
}
