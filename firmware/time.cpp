/*
 * More accurate time functions that don't rollover.
 */

#include "time.h"

#include "hw/mk20dx128.h"

// the systick interrupt increments this at 1 kHz rate
static volatile uint64_t systick_millis_count = 0;
extern "C" void systick_isr() {
    systick_millis_count++;
}

uint64_t millis64() {
    return systick_millis_count;
}

uint64_t micros64() {
    __disable_irq();
    uint32_t current = SYST_CVR;
    uint64_t count = systick_millis_count;
    uint32_t istatus = SCB_ICSR; // bit 26 indicates if systick exception pending
    __enable_irq();

    if ((istatus & SCB_ICSR_PENDSTSET) && current > 50) count++;
    current = ((F_CPU / 1000) - 1) - current;
    return count * 1000 + current / (F_CPU / 1000000);
}

void initSysticks() {
    __disable_irq();
    SYST_RVR = (F_CPU / 1000) - 1;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
    __enable_irq();
}