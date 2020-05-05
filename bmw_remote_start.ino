// sketch arduino: far lampeggiare un Led con un Pulsante 
     
#define LED_START 10  
#define LED_BRAKE 9
#define LED_KEY 8
#define LED_ENGINE 4  
#define BUTTON_LOCK 7
#define BUTTON_BRAKE 6
#define BUTTON_KEY 5

int i = 0;
     
int remote_start = 0;                 // in questo primo programma, si inizierà con un val (valore) per conservare lo stato del pin di input su 0

int lock_in_a_row = 0;
unsigned long last_lock_detected_time = 0;
unsigned long cur_lock_detected_time = 0;


int can_lock_detected_multiple(int times) //detect 3 times
{
  //just reset the timer if timeout from previous lock
  if(millis() - last_lock_detected_time > 1000) 
    lock_in_a_row = 0;
  
  //if click detected
  if(can_lock_detected() == 1) 
  {
    Serial.print("click detected, lock_in_a_row: ");
    Serial.print(lock_in_a_row + 1, DEC);
    Serial.print("\n");

    lock_in_a_row++; //increase the lock in a row counter
    last_lock_detected_time = millis(); //store the click time

    if(lock_in_a_row == times) //if we reached the number we wanted
    {
      lock_in_a_row = 0;
      last_lock_detected_time = 0;
      return 1;
    }
  }
   
  return 0;
}

int can_lock_detected() 
{
  //if it was pressed
  if(digitalRead(BUTTON_LOCK) == HIGH)
  {
    cur_lock_detected_time = millis();

    //wait 
    while(millis() - cur_lock_detected_time < 500) //if in those 500ms it showed as released, count it as a click
    {
       if(digitalRead(BUTTON_LOCK) == LOW)
        return 1; //return the click detected
    }

    return 0; //not released, count as hold
  }
  return 0; //not found as pressed
}

int can_brake_detected()
{
  return digitalRead(BUTTON_BRAKE); 
}

int can_key_detected()
{
  return digitalRead(BUTTON_KEY); 
}

void engine_start()
{
  remote_start=1;

  Serial.print("engine_start\n");

  //power the key and press start button, then wait for warm up
  // power the key relay
  digitalWrite(LED_START, HIGH);
  digitalWrite(LED_KEY, HIGH);
  delay(500);
  digitalWrite(LED_START, LOW);

  //electronics started
  for(i = 0; i < 10; i++)
  {
    digitalWrite(LED_ENGINE, HIGH);
    delay(500);
    digitalWrite(LED_ENGINE, LOW);
    delay(500);
  }

  //start the engine
  digitalWrite(LED_BRAKE, HIGH);
  digitalWrite(LED_START, HIGH);

  //wait 500ms and then the engine is started!
  delay(1000); 
  digitalWrite(LED_ENGINE, HIGH);
  digitalWrite(LED_START, LOW);
  digitalWrite(LED_BRAKE, LOW);
  digitalWrite(LED_KEY, LOW);

}

void engine_stop()
{
  remote_start=0;

  Serial.print("engine_stop\n");
  
  //start the engine
  digitalWrite(LED_START, HIGH);

  //wait 500ms and then the engine is started!
  delay(500); 
  digitalWrite(LED_ENGINE, LOW);
  digitalWrite(LED_START, LOW);
}
 
 
void setup() {  
  Serial.begin(9600);
  Serial.print("start\n");
  
  pinMode(LED_START, OUTPUT);       // questo comando imposta il pin digitale come output  
  pinMode(LED_BRAKE, OUTPUT);
  pinMode(LED_ENGINE, OUTPUT);
  pinMode(LED_KEY, OUTPUT);
  
  pinMode(BUTTON_LOCK, INPUT);     // questo comando imposta il pin digitale come input  
  pinMode(BUTTON_BRAKE, INPUT);
  pinMode(BUTTON_KEY, INPUT);

}  
 
void loop() {  

  //start with triple lock button, stop with triple lock button
  if(can_key_detected() == 0 && can_lock_detected_multiple(3) == 1)
  {
    if(remote_start == 0)
      engine_start();
    else 
      engine_stop();
  }

  //and if someone enter the car with remote start?
  if(remote_start == 1 && can_brake_detected())
  {
    //if the key is not inside the car, turn it off!
    if(can_key_detected() == 0)
      engine_stop();

    //out of remote start scope
    remote_start = 0;
  } 
}
