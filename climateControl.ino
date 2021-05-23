#include "climateControl.h"

//Global variables to be used in loop
float hif;
long temp, humidity;

void setup()
{
  //Start neccessary services
  myNex.begin(115200);
  Serial.begin(115200);
  
  startBT();
  delay(1000);
  //Read in current outide temp and humidity
  
  while(!tempSensor.begin()) 
  {
    delay(50);
  }
  
  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();
  
  // Use 1st timer (counted from zero).
  // Set 80 divider for prescaler
  Timer = timerBegin(0, 80, true);
  
  // Attach onTimer function to our timer.
  timerAttachInterrupt(Timer, &onTimer, true);
  
  // Set alarm to call triggerTempChange function every half a second (value in microseconds).
  // Repeat the alarm (third parameter)
  timerAlarmWrite(Timer, 500000, true);
  
  //Start wifi and grab local temp
  clientData Data = startWiFiGetTemp();
  temp = Data.temp;
  humidity = Data.humidity;
  elapsedTime = Data.currHours - Data.lastHours;
  
  //Start a webserver for OTA updates,
  //Really no practical purpose, but wanted some practice
  
  if(!startWeb())
  {
    Serial.println("Could not start web service!");
    ESP_RETURN_ON_ERROR(x, TAG, "fail reason 1"); 
  }
  // Start an alarm
  timerAlarmEnable(Timer);
}

void loop()
{
  float lastRead;
  //If the time since last boot is less than an hour
  //Keep the same temperature setting
  float voltage;
  
  //Start a task to read the dht sensor and pass back the temp to the main task (this)
  xTaskCreate(taskRead, "Read DHT", 2048, NULL, 1, NULL);
  
  //Create queue to put temps in
  que = xQueueCreate(2, sizeof(float));
  int x = 0;
  
  while (xQueueReceive(que, &hif, portMAX_DELAY)) 
  {
    //Check for chars in Serial 2 buffer
    myNex.NextionListen();
    //Write the current temperature goal to the display
    myNex.writeNum("tempGoal.val", int(tempGoal));

    //
    //If automatic selected
    //
    if(state == 0)
    {
      //Check if valid value was received in queue,
      //if not use the last valid value sent
      if(isnan(hif))
      {
        myNex.writeNum("currentTemp.val", int(lastRead));
      }
      else
      {
        myNex.writeNum("currentTemp.val", int(hif));
        lastRead = hif;
      }
      
      if (deviceConnected) 
      {
        pCharacteristic->setValue((uint8_t*)&tempGoal, 4);
        pCharacteristic->notify();
        delay(3); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
      }
      // Check for disconnecting
      if (!deviceConnected && oldDeviceConnected) 
      {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        oldDeviceConnected = deviceConnected;
      }
      // Check for new connection
      if (deviceConnected && !oldDeviceConnected) 
      {
        // do stuff here on connecting
        oldDeviceConnected = deviceConnected;
        delay(3);
      }
      //Read semaphore if relavent
      //Trigger a voltage change to our DAC
      if (xSemaphoreTake(timerSemaphore, 0) == pdTRUE)
      {
        uint32_t isrCount = 0, isrTime = 0;
        triggerTempChange(voltage, hif);
        // Read the interrupt count and time
        isrCount = isrCounter;
        isrTime = lastIsrAt;
        portENTER_CRITICAL(&timerMux);
        portEXIT_CRITICAL(&timerMux);
      }
      delay(100);
    }
    else
    {
      //Manual State
      //Reads current slider value and sets voltage accordingly
      voltage = myNex.readNumber("manControl.val");
      voltage = map(voltage, 0, 100, 0, 255);
    }
    
    //Write to dac output
    //Connected instead of the wiper of the temp potentiometer
    dacWrite(DAC2, voltage);
  }
}


//Check for remote IR signal to increment or decrement goal temp
    /*if (irrecv.decode(&results))
    {
      delay(40);
      Serial.print("IR_Code: ");
      Serial.println(results.value, HEX);
      bool IRRead= convertIRReading(results.value);
      if(isnan(IRRead))
      {
        //do nothing
      }
      else if(IRRead)
      {
        tempGoal += 1;
      }
      else
      {
        tempGoal -= 1;
      }
      irrecv.resume();
      delay(20);
    }*/
