# CC3200-IR-Remote-Decoding
Interfaces the CC3200 LaunchPad to an AT&amp;T infrared (IR) remote control using an IR Receiver module. Can then use the IR remote to transmit messages between boards (via UART) using T-9 style texting.

Uses an Adrafruit OLED Breakout Board - 16-bit Color 1.5" (product id: 1431), a Texas Instruments CC3200 LaunchPad (CC3200-LAUNCHXL), a Vishay TSOP31336 IR receiver module, and an AT&T S10-S3 Remote Control.

Basic Method
------------------------------
To decode the IR signals we used a combination statistical analysis and general quantitative approaches. We started off by capturing the waveforms of all pertinent signals from the various keys that’d be used in the texting (number, arrow, volume, channel, and enter keys) using our Saleae Logic Analyzer. From these waveforms we were able to determine three things: that the signals for the numeric keys and the arrow keys had drastically different lengths, some of the keys had a toggle bit to differentiate between multiple presses, and the keys themselves were being encoded using pulse distance modulation (with approximately 4 different possible widths). To determine the approximate lengths of these pulse widths and the cutoffs for the different possible values we captured multiple instances of each key and plotted them into a histogram. From this histogram we were able to very easily determine the various cutoffs for each of the ranges. For the short processing keys (the numeric keys) the cutoffs were 50,000 ticks, 37500 ticks, and 25000 ticks and for long processing keys (the arrow keys) the cutoff was 90000 ticks. These would then be used in our actual code to decode the signal being processed. 

To decode the signal our CC3200 detects all rising/falling edges produced by the Vishay IR sensor. It then takes the difference in times between the rising and falling edge to determine the width of the pulse. Once it has this pulse width it determines which range it falls into and assigns a number value to it using bit shifting/masking. Essentially, the “value” of a signal is stored inside an unsigned long long where certain bits corresponds to a particular pulse and these bits (two bits for short processing mode and 1 bit for long processing mode) are set to a particular value based on which range that pulse falls into. Once the entire signal has been processed it compares the final value to the known constant values of the keys being used. If it matches one of these values it returns the key type which is then displayed on the sender side of the OLED (the top half).

How To Setup Equipment/Wiring
------------------------------
TV CODE: 1088

Connect MOSI (pin 7 on cc3200) to SI on the OLED display.
Connect SCLK (pin 5 on cc3200) to CL on the OLED display.
Connect GPIO (pin 62 on cc3200) to DC on the OLED display.
Connect GPIO (pin 61 on cc3200) to R on the OLED display.
Connect CS (pin 8 on cc3200) to OC on the OLED display.
Connect 3.3V to Vin(+) on the OLED display.
Connect GND to GND(G) on the OLED display.

Pin 1 is the TX and Pin 2 is the RX.

Pin 63 is the GPIO Pin used to connect to the Vishay IR sensor.


How To Use Program
-------------------
When the program is running the sender simply types out the 
message that he or she wants using the IR remote and the 
standard T9 texting scheme. Each key press will be displayed
in real-time on the sender side of the OLED display (the 
top half). You may use the 'CH-' key to backspace and
delete any character. Once you're satisfied with the message
simply hit the 'CH+' to begin message transmission.

CH- == Backspace.
CH+ == Enter.
