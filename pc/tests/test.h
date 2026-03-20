/*
 * test.h — Shared test framework for XYZT PC Engine
 *
 * Each test file includes this, implements run_*_tests(),
 * and main.cu calls them all.
 */
#ifndef XYZT_TEST_H
#define XYZT_TEST_H

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../engine.h"
#include "../onetwo.h"
#include "../wire.h"
#include "../transducer.h"

extern int g_pass, g_fail;

static inline void check(const char *name, int exp, int got) {
    if (exp == got) { g_pass++; }
    else { g_fail++; printf("  FAIL %s: expected %d, got %d\n", name, exp, got); }
}

void run_core_tests(void);
void run_gpu_tests(void);
void run_lifecycle_tests(void);
void run_observer_tests(void);
void run_stress_tests(void);
void run_sense_tests(void);
void run_collision_tests(void);
void run_t3_stage1_tests(void);
void run_t3_full_tests(void);
void run_save_load_tests(void);
void run_yee_save_load_tests(void);
void run_tline_tests(void);
void run_child_conflict_tests(void);
void run_external_tests(void);
void run_lfield_test(void);
void run_stress_system_tests(void);
void run_duality_test(void);

#endif /* XYZT_TEST_H */
