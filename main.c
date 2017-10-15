/*
 * BMSBattery S series motor controllers firmware
 *
 * Copyright (C) Casainho, 2017.
 *
 * Released under the GPL License, Version 3
 */

#include <stdint.h>
#include <stdio.h>
#include "stm8s.h"
#include "gpio.h"
#include "stm8s_itc.h"
#include "stm8s_gpio.h"
#include "interrupts.h"
#include "stm8s_tim2.h"
#include "motor.h"
#include "motor_speed_controller.h"
#include "uart.h"
#include "adc.h"
#include "brake.h"
#include "utils.h"
#include "cruise_control.h"
#include "timers.h"
#include "pwm.h"
#include "config.h"

uint16_t ui16_temp_delay = 0;

uint16_t ui16_SPEED_Counter = 0; 	//time tics for speed measurement
uint16_t ui16_SPEED = 32000; 		//speed in timetics
uint16_t ui16_PAS_Counter = 0; 		//time tics for cadence measurement
uint16_t ui16_PAS = 32000;		//cadence in timetics
uint8_t ui8_PAS_Flag = 0; 		//flag for PAS interrupt
uint8_t ui8_SPEED_Flag = 0; 		//flag for SPEED interrupt


uint16_t ui16_setpoint = ADC_THROTTLE_MIN_VALUE;
uint8_t ui8_temp = 0;
uint16_t ui16_adc_value;

uint16_t ui16_throttle_timer_counter = 0;
uint16_t ui16_motor_speed_controller_timer_counter = 0;
uint16_t ui16_motor_speed = 0;

uint8_t ui8_duty_cycle_target_temp;

/////////////////////////////////////////////////////////////////////////////////////////////
//// Functions prototypes

// main -- start of firmware and main loop
int main (void);

//With SDCC, interrupt service routine function prototypes must be placed in the file that contains main ()
//in order for an vector for the interrupt to be placed in the the interrupt vector space.  It's acceptable
//to place the function prototype in a header file as long as the header file is included in the file that
//contains main ().  SDCC will not generate any warnings or errors if this is not done, but the vector will
//not be in place so the ISR will not be executed when the interrupt occurs.

//Calling a function from interrupt not always works, SDCC manual says to avoid it. Maybe the best is to put
//all the code inside the interrupt

// Local VS global variables
// Sometimes I got the following error when compiling the firmware: motor.asm:750: Error: <r> relocation error
// and the solution was to avoid using local variables and define them as global instead
// Other times, I got code that did not work until I put the variables global.

// Brake signal interrupt
void EXTI_PORTA_IRQHandler(void) __interrupt(EXTI_PORTA_IRQHANDLER);

// motor overcurrent interrupt
void EXTI_PORTD_IRQHandler(void) __interrupt(EXTI_PORTD_IRQHANDLER);

// Timer1/PWM period interrupt
void TIM1_UPD_OVF_TRG_BRK_IRQHandler(void) __interrupt(TIM1_UPD_OVF_TRG_BRK_IRQHANDLER);

/////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////

int main (void)
{
//  static uint32_t ui32_cruise_counter = 0;
//  static uint8_t ui8_cruise_duty_cycle = 0;



  //set clock at the max 16MHz
  CLK_HSIPrescalerConfig(CLK_PRESCALER_HSIDIV1);

  gpio_init ();
  brake_init ();
  while (brake_is_set()) ; // hold here while brake is pressed -- this is a protection for development
  debug_pin_init ();
  timer2_init ();
  uart_init ();
  pwm_init_6_steps ();
  hall_sensor_init ();
  adc_init ();

  enableInterrupts();

  motor_init ();
  motor_set_current_max (12); // 1 --> 0.5A
  motor_set_regen_current_max (2); // 1 --> 0.5A
  motor_set_pwm_duty_cycle_ramp_inverse_step (2); // each step = 64us
  motor_speed_controller_set_erps (0);

  hall_sensors_read_and_action (); // needed to start the motor

  while (1)
  {
    ui16_temp_delay = TIM2_GetCounter ();
    if ((ui16_temp_delay - ui16_motor_speed_controller_timer_counter) > 100) // every 100ms
    {
      ui16_motor_speed_controller_timer_counter = ui16_temp_delay;
      motor_speed_controller ();
      continue;
    }

    ui16_temp_delay = TIM2_GetCounter ();
    if ((ui16_temp_delay - ui16_throttle_timer_counter) > 20) // 20ms
    {
      ui16_throttle_timer_counter = ui16_temp_delay;

      /****************************************************************************/
      // execute cruise control
      ui8_ADC_throttle = ui8_adc_read_throttle ();
//      ui8_duty_cycle_target_temp = (uint8_t) map (ui8_ADC_throttle, ADC_THROTTLE_MIN_VALUE, ADC_THROTTLE_MAX_VALUE, 0, 255);

      ui16_motor_speed = map (ui8_ADC_throttle, ADC_THROTTLE_MIN_VALUE, ADC_THROTTLE_MAX_VALUE, 0, 600);
      motor_speed_controller_set_erps (ui16_motor_speed);

//#define DO_CRUISE_CONTROL 1
#if DO_CRUISE_CONTROL == 1
      ui8_duty_cycle_target = cruise_control (ui8_duty_cycle_target_temp);
#endif

//      motor_set_pwm_duty_cycle_target (ui8_duty_cycle_target_temp);
      /****************************************************************************/

//      printf("%d, %d, %d, %d\n",  motor_get_motor_speed_erps (), ui8_motor_state, ui8_motor_interpolation_state, ui8_adc_read_battery_voltage());
      continue;
    }
  }

  return 0;
}
