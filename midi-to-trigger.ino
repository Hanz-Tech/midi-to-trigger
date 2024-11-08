
//Output is also an echo of all incoming MIDI traffice to the Serial Out (ie, MIDI out)
//https://synthhacker.blogspot.com/2014/02/arpeggiator-fun-my-midi-to-trigger.html

typedef unsigned long micros_t;
typedef unsigned long millis_t;


// defines for MIDI Shield components only
//define KNOB1  0
//define KNOB2  1

#include <MIDI.h>
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDIserial); 

#define MIDI_CLOCK_LED  6
#define TRIGGER_OUT_LED 12
#define ANALOG_UPDATE_MILLIS 200 
#define OUTPUT_PIN_1   7
#define OUTPUT_PIN_2   8
//define analog input pins for reading the analog inputs
#define N_INPUTS (1)
int inputPin[] = {A0};
#define N_PUSHBUTTONS (3)
int pushbuttonPin[] = {2,3,4};
int prevPushbuttonState[N_PUSHBUTTONS];
boolean flag_middlePushbutton = false;

//define the midi timing info
#define MIDI_PPQN 24  //how many MIDI time pulses per quarter note...24 is standard
micros_t lastMessageReceived_micros=0;
micros_t pulseTimeout_micros = 2000000UL;

//define the outputs
#define OUTPIN_MODE  OUTPUT  //INPUT or OUTPUT?  INPUT gives more protection
#define N_OUTPUTS (1)   //how many outputs are attached
#define N_COUNTERS (N_OUTPUTS)  //the extra counter is for the LED
int pulseCountsPerOutput[N_COUNTERS]; //how many midi pulses before issuing a trigger command
int previous_pulseCountsPerOutput[N_COUNTERS];
int pulse_counter[N_COUNTERS];
int outputPin_high[] = {OUTPUT_PIN_1};
bool outputPin_state[] = {false,false};
bool isStarted = false;

 //update analog inputs after this period of milliseconds


void turnOnStatLight(int pin) {
  digitalWrite(pin,HIGH);
}
void turnOffStatLight(int pin) {
  digitalWrite(pin,LOW);
}
void activateTrigger(int pinToHigh, int LEDpin) {
    if(isStarted){
      digitalWrite(pinToHigh,HIGH);
      // Serial.println("high");
      if (LEDpin > 0) turnOnStatLight(LEDpin);
    }
}
void deactivateTrigger(int pinToLow, int LEDpin) {
    digitalWrite(pinToLow,LOW);
    // Serial.println("low");
    if (LEDpin > 0) turnOffStatLight(LEDpin);
}
void setup() {
  MIDIserial.begin(MIDI_CHANNEL_OMNI);

  //set pin modes for the lights
  pinMode(TRIGGER_OUT_LED,OUTPUT);
  pinMode(MIDI_CLOCK_LED,OUTPUT);
  
  //set pin modes for the analog inputs
  for (int I=0;I<N_INPUTS;I++) {
    pinMode(inputPin[I],INPUT);
  }

  //set pin modes for the pushbuttons
  for (int I=0;I<N_PUSHBUTTONS;I++) {
    pinMode(pushbuttonPin[I],INPUT_PULLUP);
    prevPushbuttonState[I] = HIGH; //initialize
  } 
  
  //attach interrupts to the pin
  // attachInterrupt(1,serviceMiddlePushbutton, RISING);
    
  //initialize output-related variables
  for (int I=0; I<N_COUNTERS; I++) {
    pinMode(outputPin_high[I],OUTPIN_MODE);
    pulseCountsPerOutput[I] = MIDI_PPQN; //init to 16th notes
  }
  
  //reset the counters
  resetCounters();
  
  //start serial with midi baudrate 31250
  Serial.begin(11520);
  turnOnStatLight(TRIGGER_OUT_LED);
  delay(250);
  turnOffStatLight(TRIGGER_OUT_LED);
  delay(250);
  turnOnStatLight(TRIGGER_OUT_LED);
  delay(250);
  turnOffStatLight(TRIGGER_OUT_LED);
}

void loop () {

  micros_t currentTime_micros=0;
  static boolean MIDI_LED_state = false;
  static millis_t curTime_millis=0;
  static millis_t lastMIDImessage_millis=0;
  static millis_t lastAnalogInputs_millis=0;
  static unsigned long loopCount=0;
 
     
  //check some time-based things
  loopCount++;
  if (loopCount > 25) {
    //Serial.println("loop count achieved");
    loopCount=0;
    curTime_millis = millis();
    
    //check to see if there's been any MIDI traffic...if not, shut off the LED
    if (curTime_millis < lastMIDImessage_millis) lastMIDImessage_millis = 0; //simplistic reset
    if ((curTime_millis - lastMIDImessage_millis) > 1000) turnOffStatLight(MIDI_CLOCK_LED); //turn off after 1 second
    
    //see if it's time to check the user analog inputs
    if (curTime_millis < lastAnalogInputs_millis) {
      //Serial.println("reseting lastAnalogInputs_millis");
      lastAnalogInputs_millis = 0; //simplistic reset
    }
    if ((curTime_millis - lastAnalogInputs_millis) > ANALOG_UPDATE_MILLIS) {
      lastAnalogInputs_millis = curTime_millis;
      
      //check the potentiometers
      for (int I=0;I<N_INPUTS;I++) {
        //read analog input and decide what the MIDI clock divider should be
        int val = analogRead(inputPin[I]);
        pulseCountsPerOutput[I] = convertAnalogReadToPulseDivider(val);
        if(pulseCountsPerOutput[I] != previous_pulseCountsPerOutput[I]){
          resetCounters();
          previous_pulseCountsPerOutput[I] = pulseCountsPerOutput[I];
        }

        // Serial.print("pulseCounterPerOutput ");Serial.print(I);Serial.print(": ");
        // Serial.print(val); Serial.print(", ");
        // Serial.println(pulseCountsPerOutput[I]);
      }
    }
  }

  //Are there any MIDI messages
  if (MIDIserial.read()){                // Is there a MIDI message incoming ?
    byte type = MIDIserial.getType();
    byte channel = MIDIserial.getChannel();
    byte data1 = MIDIserial.getData1();
    byte data2 = MIDIserial.getData2();
    midi::MidiType mtype = (midi::MidiType)type;
    MIDIserial.send(mtype, data1, data2, channel);
    switch(mtype){      // Get the type of the message we caught
      case midi::Clock:  //MIDI Clock Pulse
        //maybe a lot of time has passed because the cable was unplugged.
        //if so, ...check to see if enough time has passed to reset the counter
        currentTime_micros = micros();
        if (currentTime_micros < lastMessageReceived_micros) lastMessageReceived_micros = 0; //it wrapped around, simplistic reset
        if ((currentTime_micros - lastMessageReceived_micros) > pulseTimeout_micros) {
          resetCounters(); //reset the counters!
          // Serial.println("timeout");
        } 
        lastMessageReceived_micros = currentTime_micros;
      
        //loop over each channel
        for (int Icounter=0;Icounter<N_COUNTERS;Icounter++) {
          //increment the pulse counter
          pulse_counter[Icounter]++;
          
          //fit within allowed expanse for this channel
          //pulse_counter[Icounter] %= pulseCountsPerOutput[Icounter];
          int val = pulse_counter[Icounter];
          while (val > (pulseCountsPerOutput[Icounter]-1)) val -= pulseCountsPerOutput[Icounter];  //faster than mod operator
          while (val < 0)  val += pulseCountsPerOutput[Icounter]; //faster than mod operator
          pulse_counter[Icounter] = val;

          //act upon the counter
          if (pulse_counter[Icounter] == 0) {
            activateTrigger(outputPin_high[Icounter],TRIGGER_OUT_LED);
            outputPin_state[Icounter] = true;
            // Serial.println("on");
          } else if (pulse_counter[Icounter] >= min(pulseCountsPerOutput[Icounter]/2,MIDI_PPQN/2)) {
            if(outputPin_state[Icounter]){
              deactivateTrigger(outputPin_high[Icounter],TRIGGER_OUT_LED);
              outputPin_state[Icounter] = false;
            }
          }
        }
        break;
      case midi::Start:  //MIDI Clock Start
        //restart the counter
        // Serial.println("start");
        isStarted = true;
        break;
      case midi::Stop:
        isStarted = false;
        resetCounters();
        // Serial.println("stop");

    }
    
    //toggle the LED indicating serial traffic
    MIDI_LED_state = !MIDI_LED_state;
    if (MIDI_LED_state) {
      turnOnStatLight(MIDI_CLOCK_LED);   //turn on the STAT light indicating that it's received some Serial comms
    } else {
      turnOffStatLight(MIDI_CLOCK_LED);   //turn on the STAT light indicating that it's received some Serial comms
    }
    lastMIDImessage_millis = millis();    
    
  } else {
    delay(1);
  }
}

void resetCounters(void) {
  for (int I=0;I<N_COUNTERS;I++) {
    // Serial.println("reset");
    //set so that the next one will be zero
    pulse_counter[I]=-1;  
    //deactivate the Trigger so that it is ready to activate on the next MIDI pulse
    deactivateTrigger(outputPin_high[I],TRIGGER_OUT_LED);
  }
}

void stepCountersBack(void) {
  for (int I=0;I<N_COUNTERS;I++) {
    pulse_counter[I] -= 1;    
    //pulse_counter[Icounter] %= pulseCountsPerOutput[Icounter]; //fit within allowed expanse for this channel
  }
}
#define MAX_ANALOG_READ_COUNTS 1023
#define N_PULSE_OPTIONS 10
static int choices_pulsesDivider[] =  { 3,6,8,12,   16,   24,        36,  48,    72,96 };//assumed 24 PPQN
int convertAnalogReadToPulseDivider(int analogVal) {
  static int COUNTS_PER_INDEX = MAX_ANALOG_READ_COUNTS / N_PULSE_OPTIONS;
  int index = analogVal / COUNTS_PER_INDEX;  //rounds down
  index = constrain(index,0,N_PULSE_OPTIONS-1);
  return choices_pulsesDivider[index];
}
  
void serviceMiddlePushbutton(void) {
  flag_middlePushbutton=true;
}

