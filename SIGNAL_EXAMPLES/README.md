tkill.c
	demonstrates using a custom signal for robust thread cancellation
	without the modified musl, the example will hang due to the TOCTOU race condition

kill.c and alarm.c
	demonstrate using this extended musl's mechanism to ensure assured reaction to signal delivery,
	either due to kill or due to a timer.
	without the modified musl, the example will hang due to the TOCTOU race condition.


