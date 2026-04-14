/**
 * RLC Boot Self-Tests
 *
 * Power-on verification of struct packing offsets and CRC32-C correctness.
 * Called during boot before any radio activity.
 */

#pragma once

/**
 * Run all boot self-tests.
 * Logs PASS/FAIL for each test. On FAIL, logs the specific failure.
 *
 * @return 0 if all tests pass, -1 if any test fails
 */
int rlc_selftest_run(void);
