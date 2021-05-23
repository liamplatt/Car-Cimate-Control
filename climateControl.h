#include "wifi.h"
#include "nex.h"
#include <trigger.h>
#include <stdlib.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <Wire.h>
#include <Adafruit_TMP117.h>
#include <Adafruit_Sensor.h>

//Initialize temperature sensor
//TMP117 is hooked in via pins 21 and 22 on esp32, I2C device
Adafruit_TMP117  tempSensor;

//Bluetooth settings
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "c94fd806-ad95-11eb-8529-0242ac130003"
#define BTOn 3
#define BTOff 4



//Define the temp and humidity sensor params
#define DAC2 26
#define I2C_SCL 22
#define I2C_SDA 21
#define tmpAddress 0x48

//
TwoWire I2C = TwoWire(0);

//Set up hardware timer to trigger increment/decrementing of temperature
//Used to control rate of temp output change
hw_timer_t * Timer = NULL;
#define xTimerPeriod pdMS_TO_TICKS(1000)
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t isrCounter = 0;
volatile uint32_t lastIsrAt = 0;


//Declare eeprom locations and read in last values
#define EEPROMSIZE 3
int ee_tempGoal = 0;
int ee_lastVoltage = 1;
int ee_lastTime = 2;

//Initialize BLE Server
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;


//Create queue to send values in
QueueHandle_t que;

void taskRead(void *pvParams); //Function prototype for the reading of temp values from sensor

//Setup callback function to change tempGoal to data being recieved on BT
class MyServerCallbacks: public BLECharacteristicCallbacks 
{
    void onWrite(BLECharacteristic *pCharacteristic) 
    {
      uint8_t* value = pCharacteristic->getData();
      tempGoal = convertToInt(value);
      
      if(tempGoal >= 100)
      {
        tempGoal = 99;
      }
    }
};


//Function triggers on timer, notifies main loop via semaphore
void IRAM_ATTR onTimer()
{
  // Increment the counter and set the time of ISR
  isrCounter++;
  lastIsrAt = millis();
  portENTER_CRITICAL_ISR(&timerMux);
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

//Function starts bluetooth service
void startBT()
{
  //Function starts BLE service
  BLEDevice::init("esp32");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ   |
                                         BLECharacteristic::PROPERTY_WRITE  |
                                         BLECharacteristic::PROPERTY_NOTIFY |
                                         BLECharacteristic::PROPERTY_INDICATE
                                       );
  pCharacteristic->setCallbacks(new MyServerCallbacks());
  pCharacteristic->setValue("Temperature Control App");
  pService->start();
  // BLEAdvertising *pAdvertising = pServer->getAdvertising();  // this still is working for backward compatibility
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}

// Function will continuosly read in new temp values from TMP117
// Takes no argument
// Run as FreeRTOS task parallel with main
// Will send new temp values via a queue
//
void taskRead(void *pvParams)
{
  while(1)
  {
    // Read temperature as Fahrenheit (isFahrenheit = true)
    sensors_event_t t;
    tempSensor.getEvent(&t);
    //Serial.print(t.temperature);
    float f = ((t.temperature*1.8)+32);
    //Check if readings are valid
    if (isnan(f))
    {
      delay(50);
    }
    else
    {
      xQueueSend(que, &f, portMAX_DELAY);
    }
    delay(100);
  }
}
//
//Function handles the nitty gritty of deciding how to increment and decrement temperature
// Takes a current voltage passed by ref, to be changed
// Takes the current temp as float
// Returns nothing
//
void triggerTempChange(float &voltage, float hif)
{
  float diff = abs(hif - tempGoal);
  float incAmount = .05;
  int inc;
  float maxDev = 0.5;
  
  if(diff >= 20)
  {
    incAmount = 10;
  }
  else if (diff >= 10 && diff < 20)
  {
    incAmount = 5;
  }
  else if (diff >= 5 && diff < 10)
  {
    incAmount = 2;
  }
  else if (diff >= 1 && diff < 5)
  {
    incAmount = .5;
  }
  else
  {
    incAmount = .25;
  }
  if (diff >= maxDev)
    {
      if (hif > tempGoal)
      {
        //Increment Temp
        voltage += incAmount;
        inc = 1;
        if (voltage >= 255)
        {
          voltage = 255;
        }
      }
      else if (hif < tempGoal)
      {
        //Decrement Temp
        voltage -= incAmount;
        inc = 0;
        if (voltage <= 0)
        {
          voltage = 0;
        }
      }
    }
    /*else
    {
      //End timer that triggers temp changes
      //Create new timer to trigger a check for a stablized climate
      //
      //timerEnd(Timer);
      //timerAlarmEnable(TimerStable);
      //Set inc to 2, meaning its in stable state
      inc = 3; 
      float maxRead = 0;
      float sum = 0;
      float minRead = 100;
      for(int i = 0; i < sizeof(lastVoltages); i++)
      {
        if(lastVoltages[i] > maxRead)
        {
          maxRead = lastVoltages[i];
        }
        else if(lastVoltages[i] < minRead)
        {
          minRead = lastVoltages[i];
        }
        sum+= lastVoltages[i];
      }
      Serial.println(sum/sizeof(lastVoltages));
      Serial.println(maxRead);
      Serial.println(minRead);
      //Check if temp is 
      if((maxRead-minRead) <= 3)
      {
        //Dont change temp
      }
      else
      {
        if((maxRead-minRead) <= 3)
        {
          
        }
      }
    }*/
    return inc;
}
