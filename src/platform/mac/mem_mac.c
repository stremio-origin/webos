/* The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user. */
#include "platform/mem.h"
#include <stdlib.h>

/* plenty, unless SB_MEMFREE=100 asks to act like a TV short of memory */
int plat_mem_free_mb(void) { const char *e = getenv("SB_MEMFREE"); return e ? atoi(e) : 4096; }
#include <mach/mach.h>
void plat_mem_self(int *rssMb, int *swapMb) {
  mach_task_basic_info_data_t i; mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
  *rssMb = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&i, &n) == KERN_SUCCESS ? (int)(i.resident_size >> 20) : -1; *swapMb = -1;
}
