/* Standard GEM application shutdown and shell lifecycle constants. */
#ifndef GEM_LIFECYCLE_H
#define GEM_LIFECYCLE_H
#define SWM_SHUTDOWN 4
#define SWM_REZCHANGE 5
#define SWM_NEWMSG 9
#define SWM_AESMSG 10
#define SHW_SHUTDOWN 4
#define SHW_RESCHNG 5
#define SHW_INFRECGN SWM_NEWMSG
#define SHW_AESSEND SWM_AESMSG
#define SD_ABORT 0
#define SD_PARTIAL 1
#define SD_COMPLETE 2
#define NM_APTERM 0x0001
#define AP_TERM 50
#define AP_TFAIL 51
#define AP_RESCHG 57
#define SHUT_COMPLETED 60
#define RESCHG_COMPLETED 61
#define RESCH_COMPLETED RESCHG_COMPLETED
#endif
