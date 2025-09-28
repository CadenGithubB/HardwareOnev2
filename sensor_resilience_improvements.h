#ifndef SENSOR_RESILIENCE_IMPROVEMENTS_H
#define SENSOR_RESILIENCE_IMPROVEMENTS_H

// Sensor Resilience Improvements
// Addresses issues with starting multiple sensors simultaneously
// Focus: Improve existing base functions rather than create new commands

// ============================================================================
// PROBLEM ANALYSIS
// ============================================================================
/*
Current Issues:
1. I2C Bus Conflicts: 
   - Thermal needs 800kHz on Wire1
   - ToF needs 200kHz on Wire1  
   - Both try to set gWire1DefaultHz simultaneously
   
2. Resource Contention:
   - Multiple tasks created at once (thermal: 32KB, tof: 12KB, imu: 16KB)
   - Memory allocation can fail under pressure
   
3. Initialization Race Conditions:
   - Tasks try to initialize sensors simultaneously
   - I2C bus gets confused with rapid clock changes
   
4. No Startup Sequencing:
   - All sensors start immediately without coordination
   - No graceful degradation if one fails
*/

// ============================================================================
// PROPOSED IMPROVEMENTS TO EXISTING FUNCTIONS
// ============================================================================

// 1. ADD STARTUP DELAYS AND SEQUENCING
//    - Stagger sensor initialization by 500ms each
//    - Allow I2C bus to stabilize between sensor starts
//    - Priority order: IMU (Wire) -> ToF (Wire1) -> Thermal (Wire1)

// 2. IMPROVE I2C CLOCK MANAGEMENT
//    - Save/restore previous clock settings
//    - Add settling delays after clock changes
//    - Better error handling for clock conflicts

// 3. ADD MEMORY PRESSURE CHECKS
//    - Check free heap before creating tasks
//    - Fail gracefully if insufficient memory
//    - Provide clear error messages

// 4. ENHANCE ERROR RECOVERY
//    - Retry initialization on failure
//    - Reset I2C bus if needed
//    - Clean up partial initializations

// ============================================================================
// IMPLEMENTATION STRATEGY
// ============================================================================

// Modify these existing functions in HardwareOnev2.ino:
// - cmd_imustart()     - Add heap check, delay coordination
// - cmd_tofstart()     - Add I2C clock management, sequencing
// - cmd_thermalstart() - Add startup delays, better error handling

// Add these helper functions:
// - checkSensorStartupConditions() - Verify system ready for sensor start
// - coordinatedI2CClockChange()    - Safely change I2C clock with delays
// - waitForSensorInitialization()  - Better blocking with timeout/retry

// Key Principles:
// 1. Preserve existing API - no breaking changes
// 2. Add resilience through timing and coordination
// 3. Better error messages for debugging
// 4. Graceful degradation when resources limited

// ============================================================================
// SPECIFIC IMPROVEMENTS TO IMPLEMENT
// ============================================================================

/*
1. cmd_imustart() improvements:
   - Check free heap before task creation (need ~20KB minimum)
   - Add 100ms delay before enabling to let system settle
   - Better error messages distinguishing init vs task creation failures

2. cmd_tofstart() improvements:
   - Save current I2C clock before changing
   - Add 200ms delay after I2C clock change
   - Check if thermal is running and coordinate clock settings
   - Retry initialization once on failure

3. cmd_thermalstart() improvements:
   - Add 300ms startup delay (longest initialization)
   - Better coordination with ToF sensor clock conflicts
   - Check memory pressure before creating 32KB task
   - Implement exponential backoff for retries

4. Add global sensor coordination:
   - Track sensor startup sequence
   - Prevent simultaneous starts
   - Queue sensor starts if needed
*/

#endif // SENSOR_RESILIENCE_IMPROVEMENTS_H
