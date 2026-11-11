#ifndef CONFIG_H
#define CONFIG_H

#define MAX_LINE_LEN        256U
#define MAX_WORDS           16U
#define MAX_NAME_LEN        32U
#define MAX_PATH_LEN        256U
#define MAX_MSG_LEN         160U

#define MAX_NETS            512U
#define MAX_GATES           512U
#define MAX_GATE_INPUTS     4U
#define MIN_DELAY           1U
#define MAX_DELAY           1000U
#define MAX_WARNINGS        16U

#define MAX_TIME_NS         1000000000U
#define MAX_EVENTS          1024U

#define MAX_SET_LINES       256U

#define VCD_FIRST_CODE      33U
#define VCD_CODE_COUNT      94U
#define DEFAULT_OUT_DIR     "output"
#define VCD_FILE_NAME       "waves.vcd"
#define GATESIM_VERSION     "0.5"

#endif
