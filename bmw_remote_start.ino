#include <can.h>
#include <mcp2515.h>
#include <SPI.h>

//comment to enable can
#define PROTO

//define pinout of transistors
#define RELAY_START_1 2  
#define RELAY_START_2 3  
#define RELAY_BRAKE 4
#define RELAY_KEY_POWER 5
#define RELAY_KEY_LOCK 6
#define RELAY_KEY_UNLOCK 7

#define PROTO_BUTTON_LOCK 10
#define PROTO_BUTTON_BRAKE 11
#define PROTO_BUTTON_KEY 12
#define PROTO_BUTTON_ENGINE_STATUS 13


//booleans keep track of statuses read from canbus
bool status_engine_running = false;
bool status_lock_button = false;
bool status_brake_light = false;

//engine actions data
bool engine_start = false;
bool engine_stop = false;
unsigned long engine_do_start_time = 0;
unsigned long engine_do_stop_time = 0;

//remote start data
bool remote_started = false;
unsigned long remote_start_time = 0; 

//timing data
int lock_in_a_row = 0;
bool wait_for_lock_release = false;
unsigned long last_lock_detected_time = 0;
unsigned long cur_lock_detected_time = 0;

//can library data
struct can_frame canMsg;
MCP2515 mcp2515(10);

#ifdef PROTO
void proto_setup()
{
  pinMode(PROTO_BUTTON_LOCK, INPUT);
  pinMode(PROTO_BUTTON_BRAKE, INPUT);
  pinMode(PROTO_BUTTON_KEY, INPUT);
  pinMode(PROTO_BUTTON_ENGINE_STATUS, INPUT);
}

void proto_updateStatus()
{
   status_engine_running = false; //digitalRead(PROTO_BUTTON_ENGINE_STATUS) == HIGH;
   status_lock_button = digitalRead(PROTO_BUTTON_LOCK) == HIGH;
   status_brake_light = false; //digitalRead(PROTO_BUTTON_BRAKE) == HIGH;
}
#endif




/* CAN CODES
 * BREAK LIGHT
 * 0x21A : data: first bit of Byte[0] is 1 -> break light is on
 * 
 * KEYFOB 
 * 0x23A : data: xx F3 04 3F ->  lock button pressed
 * 0x23A : data: xx F3 00 3F ->  lock button released
 * 
 * ENGINE STATUS
 * 0x0A5 : data: Byte[5] = 0x00 and Byte[5] = 0x00 -> if engine is NOT running
 */


void can_updateStatus()
{
  if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
   /* Serial.print(canMsg.can_id, HEX); // print ID
    Serial.print(" "); 
    Serial.print(canMsg.can_dlc, HEX); // print DLC
    Serial.print(" ");
    
    for (int i = 0; i < canMsg.can_dlc; i++)  {  // print the data
      Serial.print(canMsg.data[i], HEX);
      Serial.print(" ");
    }

    Serial.println(); 
  */
    //doing the check!
    if(canMsg.can_id == 0x21A)       //can id for brake light
    {
      if (canMsg.data[0] & 0x80)     //first bit of first byte is 1
      {
        Serial.println("Brake on"); 
        status_brake_light = true;
      }
      else
      {
        Serial.println("Brake off"); 
        status_brake_light = false;
      }
    }
    else if(canMsg.can_id == 0x23A)  //can id for keyfob
    {
      if(   (canMsg.data[1] == 0xF3)
         && (canMsg.data[2] == 0x04)
         && (canMsg.data[3] == 0x3F)) //data = 11 F3 04 3F ->  lock button pressed
       {
         Serial.println("Lock button pressed"); 
         status_lock_button = true;
       }
      else if((canMsg.data[1] == 0xF3)
         && (canMsg.data[2] == 0x00)
         && (canMsg.data[3] == 0x3F)) //data = 11 F3 00 3F ->  lock button released
       {
         Serial.println("Lock button released"); 
         status_lock_button = false;
       }
    }
    else if(canMsg.can_id == 0x0A5)  //can id for engine status
    {
      //engine running status
      if((canMsg.data[5] == 0x00 &&
          canMsg.data[6] == 0x00)) //data: Byte[5] = 0x00 and Byte[5] = 0x00 -> if engine is NOT running
       {
         //Serial.println("Engine is NOT running"); 
         status_engine_running = false;

         
         //give one second to update canbus
         if(remote_started && (millis() - remote_start_time > 1000))
            remote_started = false;
       }
       else
       {
         Serial.println("Engine is running"); 
         status_engine_running = true;
       }
    }
  }
}

void can_setup()
{
  //reset mcp2515 and set can at 500KBPS and MCP to 8MHz quartz
  mcp2515.reset();
  mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ);

  //init the mask
  mcp2515.setFilterMask(MCP2515::MASK0, 0, 0x3ff);
  mcp2515.setFilterMask(MCP2515::MASK1, 0, 0x3ff);

  //init the filters
  mcp2515.setFilter(MCP2515::RXF0, 0, 0x21A); //break light
  mcp2515.setFilter(MCP2515::RXF1, 0, 0x23A); //keyfob
  mcp2515.setFilter(MCP2515::RXF2, 0, 0x0A5); //rpm

  //start the sniffing
  mcp2515.setNormalMode();
  
}


/* ENGINE START:
 * 1) Turn on the in-car key (2 sec)
 * 2) Press unlock button (1 sec)
 * 3) Release unlock button and wait (1 sec)
 * 4) Press lock button (1 sec)
 * 5) Release lock button and wait (1 sec)
 * 6) Press start button (1 sec)
 * 7) Release the start button and wait (1 sec)
 * 8) Start holding the brake and wait (5 sec)
 * 9) Press the start button (2 sec)
 * 10) Release the start button and wait (1 sec)
 * 11) Release the brake and wait (1 sec)
 * 12) Turn off the in-car key (1 sec)
 */

void engine_do_start()
{
  if(engine_do_start_time == 0)
  {
   engine_do_start_time = millis();
   Serial.println("*** Engine starting ***");
  }
  
  //divided in phases:
  //KEY_POWER_ON - 10 seconds
  if(millis() - engine_do_start_time < 2000)
  {
    digitalWrite(RELAY_KEY_POWER, HIGH);
    Serial.println("1) Turn on the in-car key (2 sec)");
  }
  //KEY_UNLOCK - 1 second
  else if(millis() - engine_do_start_time < 3000)
  {
    digitalWrite(RELAY_KEY_UNLOCK, HIGH);
    Serial.println("2) Press unlock button (1 sec)");
  }
  //KEY_UNLOCK_RELEASE - 1 second
  else if(millis() - engine_do_start_time < 4000)
  {
    digitalWrite(RELAY_KEY_UNLOCK, LOW);
    Serial.println("3) Release unlock button and wait (1 sec)");
  }
  //KEY_LOCK - 1 second
  else if(millis() - engine_do_start_time < 5000)
  {
    digitalWrite(RELAY_KEY_LOCK, HIGH);
    Serial.println("4) Press lock button (1 sec)");
  }
  //KEY_LOCK_RELEASE - 1 second
  else if(millis() - engine_do_start_time < 6000)
  {
    digitalWrite(RELAY_KEY_LOCK, LOW);
    Serial.println("5) Release lock button and wait (1 sec)");
  }
  //KEY_START - 1 second
  else if(millis() - engine_do_start_time < 7000)
  {
    digitalWrite(RELAY_START_1, HIGH);
    digitalWrite(RELAY_START_2, HIGH);
    Serial.println("6) Press start button (1 sec)");
  }
  //KEY_START_RELEASE - 1 second
  else if(millis() - engine_do_start_time < 8000)
  {
    digitalWrite(RELAY_START_1, LOW);
    digitalWrite(RELAY_START_2, LOW);
    Serial.println("7) Release the start button and wait (1 sec)");
  }
  //KEY_BRAKE - 5 seconds
  else if(millis() - engine_do_start_time < 13000)
  {
    digitalWrite(RELAY_BRAKE, HIGH);
    Serial.println("8) Start holding the brake and wait (5 sec)");
  }
  //KEY_START - 2 second
  else if(millis() - engine_do_start_time < 15000)
  {
    digitalWrite(RELAY_START_1, HIGH);
    digitalWrite(RELAY_START_2, HIGH);
    Serial.println("9) Press the start button (2 sec)");
  }
  //KEY_START_RELEASE - 1 second
  else if(millis() - engine_do_start_time < 16000)
  {
    digitalWrite(RELAY_START_1, LOW);
    digitalWrite(RELAY_START_2, LOW);
    Serial.println("10) Release the start button and wait (1 sec)");
  }
  //KEY_BRAKE_RELEASE - 1 second
  else if(millis() - engine_do_start_time < 17000)
  {
    digitalWrite(RELAY_BRAKE, LOW);
    Serial.println("11) Release the brake and wait (1 sec)");
  }
  //KEY_POWER_RELEASE - 1 second
  else if(millis() - engine_do_start_time < 18000)
  {
    digitalWrite(RELAY_KEY_POWER, LOW);
    Serial.println("12) Turn off the in-car key (1 sec)");
  }
  //ENGINE STARTED
  else
  {
    remote_started = true;
    remote_start_time = millis();
    engine_do_start_time = 0;
    engine_start = false;

    Serial.println("*** Engine started ***");
  }
}
  


/* ENGINE STOP:
 * 1) Press start button (1 sec)
 * 2) Release the start button and wait (1 sec)
 */

void engine_do_stop()
{
  if(engine_do_stop_time == 0)
  {
    engine_do_stop_time = millis();
    Serial.println("*** Engine stopping ***");
  }

  //KEY_START - 1 sec
  if(millis() - engine_do_stop_time < 1000)
  {
    digitalWrite(RELAY_START_1, HIGH);
    digitalWrite(RELAY_START_2, HIGH);
    Serial.println("1) Press start button (1 sec)");
  }
  //KEY_START_RELEASE - 1 sec
  else if(millis() - engine_do_stop_time < 2000)
  {
    digitalWrite(RELAY_START_1, LOW);
    digitalWrite(RELAY_START_2, LOW);
    Serial.println("2) Release the start button and wait (1 sec)");
  }
  //ENGINE STOPPED
  else
  {
    engine_do_stop_time = 0;
    engine_stop = false;
    remote_started = false;
    Serial.println("*** Engine stopped ***");
  }
}



void setup() {  
  Serial.begin(9600);
  Serial.print("[ BMW REMOTE START v1 ]\n");
  
  pinMode(RELAY_START_1, OUTPUT);       
  pinMode(RELAY_START_2, OUTPUT); 
  pinMode(RELAY_BRAKE, OUTPUT);
  pinMode(RELAY_KEY_POWER, OUTPUT);
  pinMode(RELAY_KEY_LOCK, OUTPUT);
  pinMode(RELAY_KEY_UNLOCK, OUTPUT);

  digitalWrite(RELAY_START_1, LOW);
  digitalWrite(RELAY_START_2, LOW);
  digitalWrite(RELAY_BRAKE, LOW);
  digitalWrite(RELAY_KEY_POWER, LOW);
  digitalWrite(RELAY_KEY_LOCK, LOW);
  digitalWrite(RELAY_KEY_UNLOCK, LOW);
  
#ifdef PROTO 
  proto_setup();
#else
  can_setup();
#endif
}  
 
void loop() {  

    //update status
#ifdef PROTO 
    proto_updateStatus();
#else
    can_updateStatus();
#endif

    //reset the timer if timeout from previous lock
     if(millis() - last_lock_detected_time > 1500) 
      lock_in_a_row = 0;

    //working on the triple lock click thing
    if(!wait_for_lock_release && status_lock_button) //lock clicked for first time
    {
      cur_lock_detected_time = millis(); //I save the lock button pressed
      wait_for_lock_release = true;
    }
    else if(wait_for_lock_release && !status_lock_button) //lock released for first time
    {
      wait_for_lock_release = false;
      if(millis() - cur_lock_detected_time < 1000) //if release happened in a short time (not an hold), count it as click in a row
      {
        lock_in_a_row++;
        last_lock_detected_time = millis(); //store the lock click detected time

        Serial.print("lock detected: ");
        Serial.print(lock_in_a_row, DEC);
        Serial.print("/3\n");
      }
    }

    //at third lock detected
    if(lock_in_a_row == 3) //if we reached the number we wanted
    {
      lock_in_a_row = 0;
      last_lock_detected_time = 0;

      //use triple click action only if it's not already turning on or off
      if(!engine_start && !engine_stop)
        if(!remote_started && !status_engine_running) //if not in remote start phase and the engine is not already running
        {
          //init preheating
          engine_start = true;
        }
        else
        {
          //engine stop
          engine_stop = true;
        }
    }
    
    //brake behavoir
    //if you enter the remote started car (1 sec bonus for canbus to update), exit remote start scope
    if(remote_started && (millis() - remote_start_time > 1000) && status_brake_light)
    {
      //out of remote start scope
      remote_started = false;

      Serial.print("no more remote scope\n");
    }

    //remote start timeout
    //car in remote start shutsoff after 15 minutes
    if(remote_started && (millis() - remote_start_time > (unsigned long) 1000 * 60 * 15))
    {
      //engine stop
      Serial.print("end of remote start\n");
      engine_stop = true;
      remote_started = false;
    }

    //Now that everything is decided, do the things it has to do

    

    //engine start
    if(engine_start)
      engine_do_start();

    //engine preheating
    if(engine_stop)
      engine_do_stop();
    
    
    
}
