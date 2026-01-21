#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdbool.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SYS_CLASS_DRM_DIR "/sys/class/drm"
#define GPU_METRICS_REL_PATH "device/gpu_metrics"

/*
 * throttle_status is ASIC-dependent (raw SMU FW bits).
 * indep_throttle_status is ASIC-independent and uses common SMU_THROTTLER_* bit positions.
 */

// Define the GPU metrics structure for v1.3 (as used by SMU13 dGPUs).
typedef struct {
    uint16_t structure_size;
    uint8_t format_version;
    uint8_t content_version;
    uint16_t temperature_edge;
    uint16_t temperature_hotspot;
    uint16_t temperature_mem;
    uint16_t temperature_vrgfx;
    uint16_t temperature_vrsoc;
    uint16_t temperature_vrmem;
    uint16_t average_gfx_activity;
    uint16_t average_umc_activity;
    uint16_t average_mm_activity;
    uint16_t average_socket_power;
    uint64_t energy_accumulator;
    uint64_t system_clock_counter;
    uint16_t average_gfxclk_frequency;
    uint16_t average_socclk_frequency;
    uint16_t average_uclk_frequency;
    uint16_t average_vclk0_frequency;
    uint16_t average_dclk0_frequency;
    uint16_t average_vclk1_frequency;
    uint16_t average_dclk1_frequency;
    uint16_t current_gfxclk;
    uint16_t current_socclk;
    uint16_t current_uclk;
    uint16_t current_vclk0;
    uint16_t current_dclk0;
    uint16_t current_vclk1;
    uint16_t current_dclk1;
    uint32_t throttle_status;
    uint16_t current_fan_speed;
    uint16_t pcie_link_width;
    uint16_t pcie_link_speed;
    uint16_t padding;
    uint32_t gfx_activity_acc;
    uint32_t mem_activity_acc;
    uint16_t temperature_hbm[4];
    uint64_t firmware_timestamp;
    uint16_t voltage_soc;
    uint16_t voltage_gfx;
    uint16_t voltage_mem;
    uint16_t padding1;
    uint64_t indep_throttle_status;
} gpu_metrics_v13_t;

typedef struct {
    uint8_t bit;
    const char *label;
    const char *desc;
} bit_desc_t;

static void print_u16_or_na(const char *label, uint16_t value, const char *suffix)
{
    if (value == UINT16_MAX) {
        printf("  %s: N/A\n", label);
        return;
    }

    printf("  %s: %u%s\n", label, value, suffix ? suffix : "");
}

#define MAP_INNER_WIDTH 69

static void print_map_border(void)
{
    printf("  +");
    for (int i = 0; i < MAP_INNER_WIDTH + 2; ++i)
        putchar('-');
    printf("+\n");
}

static void print_map_line(const char *text)
{
    printf("  | %-*.*s |\n", MAP_INNER_WIDTH, MAP_INNER_WIDTH, text);
}

static void print_ppt_domains_line(void);

static void print_intro(void)
{
    printf("GPU metrics quick glossary:\n");
    printf("  GFX: GPU graphics/compute engine (the main shader cores).\n");
    printf("  SoC: System-on-Chip logic (display/IO/media/control).\n");
    printf("  MM: Multimedia/VCN block (video encode/decode).\n");
    printf("  UMC: Unified Memory Controller (HBM/VRAM controller).\n");
    printf("  HBM: High Bandwidth Memory stacks on-package.\n");
    printf("  VR: Voltage regulator (power delivery components).\n");
    printf("  UCLK: memory clock (HBM/VRAM).\n");
    printf("  VCLK/DCLK: video encode/decode clocks (0 = first instance, 1 = second).\n");
    printf("  Edge temp: near the GPU edge sensor (cooler, slower-changing).\n");
    printf("  Hotspot temp: hottest on-die sensor (most conservative).\n");
    printf("  PPT0..PPT3: package power limiters (ASIC-dependent).\n");
    printf("    MI250X/Aldebaran: PPT0 = filtered/average package power,\n");
    printf("    PPT1 = raw/spike package power (per AMD SMI docs).\n");
    print_ppt_domains_line();
    printf("    Reference: https://rocmdocs.amd.com/en/latest/reference/rocm-smi.html\n");
    printf("  APCC: firmware reliability limiter (adaptive power/current control).\n");
    printf("  TDC/EDC: sustained/short-term current limits.\n");
    printf("  PROCHOT: platform over-temperature/power alarm.\n");
    printf("  GFX Activity Acc: accumulator (firmware-defined units; use deltas).\n");
    printf("  MEM Activity Acc: accumulator (firmware-defined units; use deltas).\n");
    printf("  N/A: firmware did not report this field (value 0xFFFF).\n");

    printf("\nApproximate physical map (not to scale):\n");
    print_map_border();
    print_map_line("GPU package");
    print_map_line("");
    print_map_line("[GFX/Compute]    [SoC/IO]                 [HBM0][HBM1][HBM2][HBM3]");
    print_map_line("    |                |                        |   |   |   |");
    print_map_line("Edge/Hotspot       SoC temp                     HBM temps");
    print_map_line("    |                |");
    print_map_line(" VR GFX            VR SoC                VR MEM (power delivery)");
    print_map_line("");
    print_map_line("PCIe link (width/speed)");
    print_map_border();
    printf("\n");
}

/*
 * Common ASIC-independent mapping (SMU_THROTTLER_* bits in amdgpu_smu.h).
 * These bits are stable across ASICs and are what indep_throttle_status uses.
 */
static const bit_desc_t indep_throttler_bits[] = {
    {0,  "PPT0", "pkg power (avg/filtered)"},
    {1,  "PPT1", "pkg power (raw/spike)"},
    {2,  "PPT2", "power limit"},
    {3,  "PPT3", "power limit"},
    {4,  "SPL", "socket power limit"},
    {5,  "FPPT", "fast power limit"},
    {6,  "SPPT", "sustained power limit"},
    {7,  "SPPT_APU", "APU power limit"},
    {16, "TDC_GFX", "current limit (gfx)"},
    {17, "TDC_SOC", "current limit (soc)"},
    {18, "TDC_MEM", "current limit (mem)"},
    {19, "TDC_VDD", "current limit (vdd)"},
    {20, "TDC_CVIP", "current limit (cvip)"},
    {21, "EDC_CPU", "current limit (cpu)"},
    {22, "EDC_GFX", "current limit (gfx)"},
    {23, "APCC", "reliability limit"},
    {32, "TEMP_GPU", "temperature (gpu)"},
    {33, "TEMP_CORE", "temperature (core)"},
    {34, "TEMP_MEM", "temperature (mem)"},
    {35, "TEMP_EDGE", "temperature (edge)"},
    {36, "TEMP_HOTSPOT", "temperature (hotspot)"},
    {37, "TEMP_SOC", "temperature (soc)"},
    {38, "TEMP_VR_GFX", "temperature (vr gfx)"},
    {39, "TEMP_VR_SOC", "temperature (vr soc)"},
    {40, "TEMP_VR_MEM0", "temperature (vr mem0)"},
    {41, "TEMP_VR_MEM1", "temperature (vr mem1)"},
    {42, "TEMP_LIQUID0", "temperature (liquid0)"},
    {43, "TEMP_LIQUID1", "temperature (liquid1)"},
    {44, "VRHOT0", "vr hot"},
    {45, "VRHOT1", "vr hot"},
    {46, "PROCHOT_CPU", "cpu prochot"},
    {47, "PROCHOT_GFX", "gpu prochot"},
    {56, "PPM", "power management"},
    {57, "FIT", "reliability limit"},
};

/*
 * ASIC-dependent mapping for Aldebaran (SMU13, SMC FW 68.xx).
 * Adjust this table if your ASIC differs.
 */
static const bit_desc_t ald_throttle_bits[] = {
    {0,  "PPT0", "pkg power (avg/filtered)"},
    {1,  "PPT1", "pkg power (raw/spike)"},
    {2,  "TDC_GFX", "current limit (gfx)"},
    {3,  "TDC_SOC", "current limit (soc)"},
    {4,  "TDC_HBM", "current limit (hbm)"},
    {6,  "TEMP_GPU", "temperature (gpu)"},
    {7,  "TEMP_MEM", "temperature (mem)"},
    {11, "TEMP_VR_GFX", "temperature (vr gfx)"},
    {12, "TEMP_VR_SOC", "temperature (vr soc)"},
    {13, "TEMP_VR_MEM", "temperature (vr mem)"},
    {19, "APCC", "reliability limit"},
};

static void print_ppt_domains_line(void)
{
    const bit_desc_t *bits[4];
    size_t count = 0;

    for (size_t i = 0; i < sizeof(ald_throttle_bits) / sizeof(ald_throttle_bits[0]); ++i) {
        const char *label = ald_throttle_bits[i].label;
        if (strncmp(label, "PPT", 3) == 0 && count < 4)
            bits[count++] = &ald_throttle_bits[i];
    }

    if (count == 0) {
        printf("  PPT domains present (ASIC map): none detected\n");
        return;
    }

    printf("  PPT domains present (ASIC map): ");
    for (size_t i = 0; i < count; ++i) {
        printf("%s%s", i ? ", " : "", bits[i]->label);
        if (bits[i]->desc)
            printf(" (%s)", bits[i]->desc);
    }
    printf("\n");
}

static void print_set_bits64(const char *label, uint64_t value,
                            const bit_desc_t *bits, size_t bit_count)
{
    size_t i;
    int printed = 0;

    if (value == UINT64_MAX) {
        printf("  %s: 0x%016" PRIx64 " (unavailable)\n", label, value);
        return;
    }

    printf("  %s: 0x%016" PRIx64 "\n", label, value);
    printf("  %s reasons:", label);
    for (i = 0; i < bit_count; ++i) {
        if (value & (1ULL << bits[i].bit)) {
            printf("%s %s%s%s",
                   printed ? "," : "",
                   bits[i].label,
                   bits[i].desc ? " (" : "",
                   bits[i].desc ? bits[i].desc : "");
            if (bits[i].desc)
                printf(")");
            printed = 1;
        }
    }
    printf("%s\n", printed ? "" : " none");
}

static void print_set_bits32(const char *label, uint32_t value,
                            const bit_desc_t *bits, size_t bit_count)
{
    size_t i;
    int printed = 0;

    printf("  %s: 0x%08" PRIx32 "\n", label, value);
    printf("  %s reasons:", label);
    for (i = 0; i < bit_count; ++i) {
        if (value & (1U << bits[i].bit)) {
            printf("%s %s%s%s",
                   printed ? "," : "",
                   bits[i].label,
                   bits[i].desc ? " (" : "",
                   bits[i].desc ? bits[i].desc : "");
            if (bits[i].desc)
                printf(")");
            printed = 1;
        }
    }
    printf("%s\n", printed ? "" : " none");
}

static void print_gpu_metrics(int card_id, const gpu_metrics_v13_t *metrics)
{
    printf("\nGPU Metrics for Card %d:\n", card_id);
    printf("  Structure Size: %u bytes\n", metrics->structure_size);
    printf("  Format Version: %u\n", metrics->format_version);
    printf("  Content Version: %u\n", metrics->content_version);
    print_u16_or_na("Temperature (Edge)", metrics->temperature_edge, " C");
    print_u16_or_na("Temperature (Hotspot)", metrics->temperature_hotspot, " C");
    print_u16_or_na("Temperature (Memory)", metrics->temperature_mem, " C");
    print_u16_or_na("Temperature (VR GFX)", metrics->temperature_vrgfx, " C");
    print_u16_or_na("Temperature (VR SoC)", metrics->temperature_vrsoc, " C");
    print_u16_or_na("Temperature (VR MEM)", metrics->temperature_vrmem, " C");
    print_u16_or_na("Average GFX Activity", metrics->average_gfx_activity, " %");
    print_u16_or_na("Average UMC Activity", metrics->average_umc_activity, " %");
    print_u16_or_na("Average MM Activity", metrics->average_mm_activity, " %");
    print_u16_or_na("Average Socket Power", metrics->average_socket_power, " W");
    printf("  Energy Accumulator: %" PRIu64 "\n", metrics->energy_accumulator);
    printf("  System Clock Counter: %" PRIu64 " ns\n", metrics->system_clock_counter);
    print_u16_or_na("Average GFX Clock", metrics->average_gfxclk_frequency, " MHz");
    print_u16_or_na("Average SOC Clock", metrics->average_socclk_frequency, " MHz");
    print_u16_or_na("Average UCLK", metrics->average_uclk_frequency, " MHz");
    print_u16_or_na("Average VCLK0", metrics->average_vclk0_frequency, " MHz");
    print_u16_or_na("Average DCLK0", metrics->average_dclk0_frequency, " MHz");
    print_u16_or_na("Average VCLK1", metrics->average_vclk1_frequency, " MHz");
    print_u16_or_na("Average DCLK1", metrics->average_dclk1_frequency, " MHz");
    print_u16_or_na("Current GFX Clock", metrics->current_gfxclk, " MHz");
    print_u16_or_na("Current SOC Clock", metrics->current_socclk, " MHz");
    print_u16_or_na("Current UCLK", metrics->current_uclk, " MHz");
    print_u16_or_na("Current VCLK0", metrics->current_vclk0, " MHz");
    print_u16_or_na("Current DCLK0", metrics->current_dclk0, " MHz");
    print_u16_or_na("Current VCLK1", metrics->current_vclk1, " MHz");
    print_u16_or_na("Current DCLK1", metrics->current_dclk1, " MHz");
    print_u16_or_na("Fan Speed", metrics->current_fan_speed, " RPM");
    print_u16_or_na("PCIe Link Width", metrics->pcie_link_width, "");
    printf("  PCIe Link Speed: %.1f GT/s (raw %u)\n",
           metrics->pcie_link_speed / 10.0,
           metrics->pcie_link_speed);
    printf("  GFX Activity Acc: %" PRIu32 "\n", metrics->gfx_activity_acc);
    printf("  MEM Activity Acc: %" PRIu32 "\n", metrics->mem_activity_acc);
    for (size_t i = 0; i < sizeof(metrics->temperature_hbm) / sizeof(metrics->temperature_hbm[0]); ++i) {
        char label[32];
        snprintf(label, sizeof(label), "Temperature (HBM%zu)", i);
        print_u16_or_na(label, metrics->temperature_hbm[i], " C");
    }
    printf("  Firmware Timestamp: %" PRIu64 " (10ns)\n", metrics->firmware_timestamp);
    print_u16_or_na("Voltage (SoC)", metrics->voltage_soc, " mV");
    print_u16_or_na("Voltage (GFX)", metrics->voltage_gfx, " mV");
    print_u16_or_na("Voltage (Memory)", metrics->voltage_mem, " mV");

    printf("  Note: throttle_status is ASIC-dependent; indep_throttle_status is normalized.\n");
    /*
     * throttle_status is raw (ASIC-specific). Here we decode it as Aldebaran;
     * update the table if your ASIC differs.
     */
    print_set_bits32("throttle_status", metrics->throttle_status,
                    ald_throttle_bits, sizeof(ald_throttle_bits) / sizeof(ald_throttle_bits[0]));

    /* indep_throttle_status uses common SMU_THROTTLER_* bit positions. */
    print_set_bits64("indep_throttle_status", metrics->indep_throttle_status,
                    indep_throttler_bits, sizeof(indep_throttler_bits) / sizeof(indep_throttler_bits[0]));
}

static int parse_card_id(const char *name, int *card_id)
{
    const char *p;
    char *end = NULL;
    long value;

    if (strncmp(name, "card", 4) != 0)
        return 0;

    p = name + 4;
    if (*p == '\0')
        return 0;

    for (const char *q = p; *q != '\0'; ++q) {
        if (!isdigit((unsigned char)*q))
            return 0;
    }

    errno = 0;
    value = strtol(p, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < 0 || value > INT32_MAX)
        return 0;

    *card_id = (int)value;
    return 1;
}

static int parse_card_index(const char *arg, int *card_id)
{
    char *end = NULL;
    long value;

    if (!arg || *arg == '\0')
        return 0;

    errno = 0;
    value = strtol(arg, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < 0 || value > INT32_MAX)
        return 0;

    *card_id = (int)value;
    return 1;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [--all] [-c N | --card N | --card=N]\n", prog);
    printf("  --all            Scan all cards under /sys/class/drm (default)\n");
    printf("  -c N, --card N   Show only card N\n");
    printf("  --legend         Print glossary and ASCII map, then continue\n");
    printf("  -h, --help       Show this help\n");
}

int main(int argc, char **argv)
{
    int requested_card = -1;
    bool list_all = true;
    bool found_requested = false;
    bool show_legend = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--legend") == 0) {
            show_legend = true;
            continue;
        }
        if (strcmp(argv[i], "--all") == 0) {
            requested_card = -1;
            list_all = true;
            continue;
        }
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--card") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing card index after %s\n", argv[i]);
                return EXIT_FAILURE;
            }
            if (!parse_card_index(argv[i + 1], &requested_card)) {
                fprintf(stderr, "Invalid card index: %s\n", argv[i + 1]);
                return EXIT_FAILURE;
            }
            list_all = false;
            ++i;
            continue;
        }
        if (strncmp(argv[i], "--card=", 7) == 0) {
            if (!parse_card_index(argv[i] + 7, &requested_card)) {
                fprintf(stderr, "Invalid card index: %s\n", argv[i] + 7);
                return EXIT_FAILURE;
            }
            list_all = false;
            continue;
        }

        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (show_legend)
        print_intro();

    DIR *dir;
    struct dirent *ent;
    int found = 0;

    dir = opendir(SYS_CLASS_DRM_DIR);
    if (!dir) {
        fprintf(stderr, "Error opening %s: %s\n", SYS_CLASS_DRM_DIR, strerror(errno));
        return EXIT_FAILURE;
    }

    while ((ent = readdir(dir)) != NULL) {
        int card_id;
        char path[PATH_MAX];
        struct stat st;

        if (!parse_card_id(ent->d_name, &card_id))
            continue;
        if (!list_all && card_id != requested_card)
            continue;

        snprintf(path, sizeof(path), "%s/%s/%s", SYS_CLASS_DRM_DIR, ent->d_name, GPU_METRICS_REL_PATH);
        if (stat(path, &st) != 0) {
            if (errno != ENOENT)
                fprintf(stderr, "Error stating %s: %s\n", path, strerror(errno));
            continue;
        }

        if (!S_ISREG(st.st_mode))
            continue;

        FILE *file = fopen(path, "rb");
        if (!file) {
            fprintf(stderr, "Error opening %s: %s\n", path, strerror(errno));
            continue;
        }

        gpu_metrics_v13_t metrics;
        size_t read_size = fread(&metrics, 1, sizeof(metrics), file);
        fclose(file);

        if (read_size < sizeof(metrics)) {
            fprintf(stderr,
                    "Error reading GPU metrics for card %d: expected %zu bytes, read %zu bytes\n",
                    card_id, sizeof(metrics), read_size);
            continue;
        }

        print_gpu_metrics(card_id, &metrics);
        found++;
        if (!list_all && card_id == requested_card) {
            found_requested = true;
            break;
        }
    }

    closedir(dir);

    if (!list_all && !found_requested) {
        fprintf(stderr, "Card %d not found or no gpu_metrics available\n", requested_card);
        return EXIT_FAILURE;
    }

    if (found == 0) {
        fprintf(stderr, "No gpu_metrics files found under %s\n", SYS_CLASS_DRM_DIR);
    }

    return EXIT_SUCCESS;
}
