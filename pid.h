//pid.h
#pragma once

class Pid {
 private:
    double *pv;  	/*pointer to an integer that contains the process value*/
    double *sp;  	/*pointer to an integer that contains the set point*/ 
    float integral;
    float pgain;
    float igain;
    float dgain;
    int deadband;
    int last_error;
 public:
		/*----------------------------------------------------------------- -------
	pid_init

	DESCRIPTION   This function initializes the pointers in the _pid structure
	              to the process variable and the setpoint.  *pv and *sp are
	              integer pointers.
	------------------------------------------------------------------- -----*/
	static void pid_init( _pid* a, double* pv, double* sp);
	/*----------------------------------------------------------------- -------
	pid_tune

	DESCRIPTION   Sets the proportional gain (p_gain), integral gain (i_gain),
	              derivitive gain (d_gain), and the dead band (dead_band) of
	              a pid control structure _pid.
	------------------------------------------------------------------- -----*/
	static void pid_tune(_pid *a, float p_gain, float i_gain, float d_gain, int dead_band);
	/*----------------------------------------------------------------- -------
	get_gains

	DESCRIPTION   Returns the gains and dead band in a _pid control structure
	              in the locations pointed to by the p_gain, i_gain, d_gain,
	              and dead_band pointers.
	              
	ALSO SEE      pid_tune              
	------------------------------------------------------------------- -----*/
	static void get_gains(_pid *a, float *p_gain, float *i_gain, float *d_gain, int *dead_band);
	/*----------------------------------------------------------------- -------
	pid_setinteg

	DESCRIPTION   Set a new value for the integral term of the pid equation.
	              This is useful for setting the initial output of the
	              pid controller at start up.
	------------------------------------------------------------------- -----*/
	static void pid_setinteg(_pid *a, float new_integ);
	/*----------------------------------------------------------------- -------
	pid_bumpless

	DESCRIPTION   Bumpless transfer algorithim.  When suddenly changing
	              setpoints, or when restarting the PID equation after an
	              extended pause, the derivative of the equation can cause 
	              a bump in the controller output.  This function will help 
	              smooth out that bump. The process value in *pv should
	              be the updated just before this function is used.
	------------------------------------------------------------------- -----*/
	static void pid_bumpless(_pid *a);
	    
	/*----------------------------------------------------------------- -------
	pid_calc

	DESCRIPTION   Performs PID calculations for the _pid structure *a.  This
	              function uses the positional form of the pid equation, and
	              incorporates an integral windup prevention algorithim.
	              Rectangular integration is used, so this function must be
	              repeated on a consistent time basis for accurate control.

	RETURN VALUE  The new output value for the pid loop.

	USAGE         #include "control.h"
	              main() {
	                   struct _pid PID;
	                   int process_variable, set_point;
	                   pid_init(&PID, &process_variable, &set_point);
	                   pid_tune(&PID, 4.3, 0.2, 0.1, 2);
	                   set_point = 500;
	                   pid_setinteg(&PID,30.0);
	                   process_variable = read_temp();
	                   pid_bumpless(&PID);
	                   for(;;) {
	                        process_variable = read_temp();
	                        output( pid_calc(&PID) );
	                        wait(1.0);
	                   }
	              }
	------------------------------------------------------------------- -----*/
	static float pid_calc(_pid *a);

	//END PID Controller
};