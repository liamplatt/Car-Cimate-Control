#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"

//Weather and time api config
const String weatherURL = "http://api.openweathermap.org/data/2.5/weather?q=Hartford,USA&APPID=a17bc695af3b7d64da5afecba1f6b511";
const char* timeURL = "pool.ntp.org";
const long gmtOffset_sec = 18000;
const long daylightOffset_sec = 0;

//Wifi Settings
const char* host = "esp32";
const char* ssid = "xxxx";
const char* password = "xxxx";

//Struct to contain temp and humidity data and time info, parsed from GET requests
struct clientData {
  long temp;
  long humidity;
  int lastHours;
  int currHours;
};

//Function protos
String httpGETRequest(const char* serverName); //Function returns
int startWeb(); //Function starts a web server for OTA updates, returns 0 if successful
int getHour(); //Function returns the current hour 0-23
clientData startWiFiGetTemp(); //Starts up a WiFi connection, connects to open-source web api, and returns weather info/time
bool readResponseContent(clientData &Client); //Parses through weather api, and returns temperature and humidity
int convertToInt(uint8_t * uInt); //Converts an unsigned int to an integer

//Initialize a webserver for OTA updates
WebServer server(80);

/*
 * Login page (from example)
 */
const char* loginIndex =
 "<form name='loginForm'>"
    "<table width='20%' bgcolor='A09F9F' align='center'>"
        "<tr>"
            "<td colspan=2>"
                "<center><font size=4><b>ESP32 Login Page</b></font></center>"
                "<br>"
            "</td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
             "<td>Username:</td>"
             "<td><input type='text' size=25 name='userid'><br></td>"
        "</tr>"
        "<br>"
        "<br>"
        "<tr>"
            "<td>Password:</td>"
            "<td><input type='Password' size=25 name='pwd'><br></td>"
            "<br>"
            "<br>"
        "</tr>"
        "<tr>"
            "<td><input type='submit' onclick='check(this.form)' value='Login'></td>"
        "</tr>"
    "</table>"
"</form>"
"<script>"
    "function check(form)"
    "{"
    "if(form.userid.value=='admin' && form.pwd.value=='admin')"
    "{"
    "window.open('/serverIndex')"
    "}"
    "else"
    "{"
    " alert('Error Password or Username')/*displays error message*/"
    "}"
    "}"
"</script>";

/*
 * Server Index Page (from example)
 */
const char* serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";

//From example file
int startWeb()
 {
 if (!MDNS.begin(host)) { //http://esp32.local
    //Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  //Serial.println("mDNS responder started");
  /*return index page which is stored in serverIndex */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      //Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        //Update.printError(Serial);
        return 1;
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        //Update.printError(Serial);
        return 1;
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        //Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        //Update.printError(Serial);
        return 1;
      }
    }
  });
  server.begin();
  return 0;
 }
 
//Function to convert an uint8 pointer to an integer
//Returns: integer
int convertToInt(uint8_t * uInt)
{
  String str = "";
  char c;
  for (int i = 0; i < 4; i++)
  {
    if (isDigit(c = (char)(*(uInt + i))))
    {
      str += c;
    }
  }
  return str.toInt();
}
//
//Returns the current hour, 
//for checking wether to grab eeprom memory temperature settings
//
int getHour()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return -1;
  }
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  String timeHourStr = (String) timeHour;
  return timeHourStr.toInt();
}

//
//Function to start wifi, 
//Will also grab current hour to pass to struct
//Passes clientData Struct back to setup loop
//
clientData startWiFiGetTemp()
{
  WiFi.begin(ssid, password);
  delay(1000);
  int i = 0;
  clientData Data;
  Data.temp = 0;
  Data.humidity = 0;
  while(WiFi.status() != WL_CONNECTED && i < 50)
  {
    Serial.print(".");
    delay(150);
    i++;
  }
  if(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Could not connect");
  }
  else
  {
    Serial.println("Connected!");
    configTime(gmtOffset_sec, daylightOffset_sec, timeURL);
    Data.currHours = getHour();
    
    delay(1000);
    while(!readResponseContent(Data))
    {
      Serial.println("Failed to grab parse data");
      delay(500);
    }
    delay(3000);
  }
  return Data;
}

//
//Function grabs a String containing GET request info
//Returns a string to be parsed
//
String httpGETRequest(const char* serverName) 
{
  
  HTTPClient http;
  // Your IP address with path or Domain name with URL path 
  http.begin(serverName);

  // Send HTTP POST request
  int httpResponseCode = http.GET();

  String payload = "{}"; 

  if (httpResponseCode>0) {
    //Serial.print("HTTP Response code: ");
    //Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else {
    //Serial.print("Error code: ");
    //Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

bool readResponseContent(clientData &Client) 
{
  // Compute optimal size of the JSON buffer according to what we need to parse.
  DynamicJsonDocument jsonBuffer(24576);
  String requestResponse = httpGETRequest(weatherURL.c_str());
  //Serial.println(requestResponse);
  DeserializationError err = deserializeJson(jsonBuffer, requestResponse);
  Client.temp = (1.8*(jsonBuffer["main"]["temp"].as<long>() -273.15)) + 32;
  Client.humidity = jsonBuffer["main"]["humidity"].as<long>();
  if(err)
  {
    //Serial.println(err.f_str());
    return false;
  }
  
  return true;
}
