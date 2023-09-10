#include <ArduinoOTA.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <OpenTherm.h>
#include "config.h"
#include "index.html.h"

constexpr uint8_t maxWifiReconnectAttempts = 20;
constexpr unsigned long minWifiCheckTimeout = 500;

constexpr const char* const enableColor = "green";
constexpr const char* const enableText = "вкл.";
constexpr const char* const disableColor = "red";
constexpr const char* const disableText = "выкл.";
constexpr const char* const checkboxChecked = "checked";
constexpr const char* const checkboxUnchecked = "";
constexpr const char* const successText = "успешно";
constexpr const char* const errorText = "ошибка";

EEPROMConfig config(0);

// Wifi AP configuration
constexpr const char* const wifiAPSsid = "Boiler01";
constexpr const char* const wifiAPPass = "boiler-server";

// OTA configuration
constexpr uint16_t otaPort = 8232;
constexpr const char* const otaHostname = "esp-boiler";
constexpr const char* const otaPassword = "boiler-esp";

// Web server configuration
ESP8266WebServer server(80);
// AP address
IPAddress apIp(192, 168, 4, 11);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

constexpr int inPin = 4;
constexpr int outPin = 5;
OpenTherm ot(inPin, outPin);

// Runtime variables
enum class WifiConnectionStatus
{
  unknown = 0,
  connecting = 1,
  connected = 2,
};
WifiConnectionStatus wifiConnectionStatus = WifiConnectionStatus::unknown;
uint8_t wifiReconnectAttempts = 0;
unsigned long lastWifiTime;
unsigned long boardTime;
float externalTemp = 25;
float priorExternalTemp = 0;
unsigned long lastExternalTempUpdateTime;
unsigned long priorExternalTempUpdateTime;
bool isCentralHeating;
bool isHotWater;
bool isCooling;
bool isFlame;
float actualBoilerTemp;
float boilerTemp;
bool lastSetupTemp;
OpenThermResponseStatus lastSetupStatus;
char* htmlAnswer;
float integralError = 0;

bool wifiConnect();
void configureOTA();
void handleRoot();
void handleTemp();
float parseFloat(String);
float pid(float, float, float, float&, float);

void IRAM_ATTR handleInterrupt()
{
  ot.handleInterrupt();
}

void setup()
{
#ifdef NEED_SERIAL_PRINT
  Serial.begin(115200);
#endif

#ifdef NEED_SERIAL_PRINT
  Serial.print("Init EEPROM with size = ");
  Serial.println(sizeof(ConfigData));
#endif
  EEPROM.begin(sizeof(ConfigData));

  config.read();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIp, apGateway, apSubnet);
  WiFi.softAP(wifiAPSsid, wifiAPPass);
  lastWifiTime = millis();
  wifiConnect();

  htmlAnswer = (char*)malloc(4096);

  server.on("/", handleRoot);
  server.on("/temp", handleTemp);
  server.begin();
#ifdef NEED_SERIAL_PRINT
  Serial.println("HTTP server started");
#endif

  configureOTA();

  ot.begin(handleInterrupt);
#ifdef NEED_SERIAL_PRINT
  Serial.println("OpenTherm initialized");
#endif

  boardTime = millis();
}

void loop()
{
  auto passedTime = millis() - boardTime;

  if (passedTime > 1000)
  {
#ifdef NEED_SERIAL_PRINT
    Serial.print("Setup status: centralHeating=");
    Serial.print(config.data.centralHeating);
    Serial.print("; hotWater=");
    Serial.print(config.data.hotWater);
    Serial.print("; cooling=");
    Serial.println(config.data.cooling);
#endif
    unsigned long response = ot.setBoilerStatus(config.data.centralHeating, config.data.hotWater, config.data.cooling);
    lastSetupStatus = ot.getLastResponseStatus();
    if (lastSetupStatus != OpenThermResponseStatus::SUCCESS)
    {
#ifdef NEED_SERIAL_PRINT
      Serial.print("Error: Invalid boiler response = ");
      Serial.print(response, HEX);
      Serial.print(", last response status = ");
      Serial.println(lastSetupStatus);
#endif
    }
    isCentralHeating = ot.isCentralHeatingActive(response);
    isHotWater = ot.isHotWaterActive(response);
    isCooling = ot.isCoolingActive(response);
    isFlame = ot.isFlameOn(response);

    if (config.data.manualBoilerTemp)
    {
      boilerTemp = config.data.desiredBoilerTemp;
    }
    else
    {
      // TODO: calc boiler temp using PID
      boilerTemp = pid(config.data.desiredTemp, externalTemp, priorExternalTemp, integralError, (lastExternalTempUpdateTime - priorExternalTempUpdateTime) / 1000.0);
    }


#ifdef NEED_SERIAL_PRINT
    Serial.print("Setup boiler temp = ");
    Serial.print(boilerTemp);
    Serial.println("°C");
#endif
    lastSetupTemp = ot.setBoilerTemperature(boilerTemp);
    if (!lastSetupTemp)
    {
#ifdef NEED_SERIAL_PRINT
      Serial.print("Error: Can't setup the boiler temperature, last response status = ");
      Serial.println(ot.getLastResponseStatus());
#endif
    }
    actualBoilerTemp = ot.getBoilerTemperature();

    boardTime = millis();
  }

  wifiConnect();
  server.handleClient();
  ArduinoOTA.handle();
}

bool wifiConnect()
{
  if (wifiConnectionStatus != WifiConnectionStatus::unknown && WiFi.isConnected())
  {
    if (wifiConnectionStatus != WifiConnectionStatus::connected)
    {
#ifdef NEED_SERIAL_PRINT
      Serial.print("WiFi connected. IP address: ");
      Serial.println(WiFi.localIP());
#endif
      wifiConnectionStatus = WifiConnectionStatus::connected;
    }
    return true;
  }

  if (wifiConnectionStatus == WifiConnectionStatus::connecting)
  {
    if (millis() - lastWifiTime > minWifiCheckTimeout)
    {
#ifdef NEED_SERIAL_PRINT
      Serial.print("WiFi connection attempt #");
      Serial.println(wifiReconnectAttempts + 1);
#endif

      if (wifiReconnectAttempts++ > maxWifiReconnectAttempts)
      {
        wifiConnectionStatus = WifiConnectionStatus::unknown;

#ifdef NEED_SERIAL_PRINT
        Serial.println(" WiFi connection failed!");
#endif
      }

      lastWifiTime = millis();
    }
    return false;
  }

#ifdef NEED_SERIAL_PRINT
  Serial.print("Connecting to ");
  Serial.println(config.data.ssid);
#endif

  wifiConnectionStatus = WifiConnectionStatus::connecting;
  wifiReconnectAttempts = 0;
  WiFi.disconnect();
  WiFi.begin(config.data.ssid, config.data.pass);
  return WiFi.isConnected();
}

void configureOTA()
{
  ArduinoOTA.setPort(otaPort);
  ArduinoOTA.setHostname(otaHostname);
  ArduinoOTA.setPassword(otaPassword);

  ArduinoOTA.onStart([]() {
#ifdef NEED_SERIAL_PRINT
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    {
      type = "filesystem";
    }
    Serial.println("OTA | Start updating " + type);
#endif
  });

  ArduinoOTA.onEnd([]() {
#ifdef NEED_SERIAL_PRINT
    Serial.println("\nOTA | End");
#endif
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
#ifdef NEED_SERIAL_PRINT
    Serial.printf("OTA | Progress: %u%%\r", (progress / (total / 100)));
#endif
  });

  ArduinoOTA.onError([](ota_error_t error) {
#ifdef NEED_SERIAL_PRINT
    Serial.printf("OTA | Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
#endif
  });

  ArduinoOTA.begin();
}

void handleRoot()
{
  bool update = false;
  if (server.method() == HTTP_POST && !server.arg("ssid").isEmpty() && !server.arg("pass").isEmpty())
  {
#ifdef NEED_SERIAL_PRINT
    Serial.print("Setup WiFi params: ssid = ");
    Serial.print(server.arg("ssid"));
    Serial.print("; pass = ");
    Serial.println(server.arg("pass"));
#endif
    strcpy(config.data.ssid, server.arg("ssid").c_str());
    strcpy(config.data.pass, server.arg("pass").c_str());
    wifiConnectionStatus = WifiConnectionStatus::unknown;
    update = true;
  }

  if (server.method() == HTTP_POST && !server.arg("desiredTemp").isEmpty())
  {
    config.data.desiredTemp = parseFloat(server.arg("desiredTemp"));
#ifdef NEED_SERIAL_PRINT
    Serial.print("Setup desiredTemp: ");
    Serial.print(server.arg("desiredTemp"));
    Serial.print("°C / parsed: ");
    Serial.print(config.data.desiredTemp);
    Serial.println("°C");
#endif

    config.data.desiredBoilerTemp = parseFloat(server.arg("boilerTemp"));
#ifdef NEED_SERIAL_PRINT
    Serial.print("Setup desiredBoilerTemp: ");
    Serial.print(server.arg("boilerTemp"));
    Serial.print("°C / parsed: ");
    Serial.print(config.data.desiredBoilerTemp);
    Serial.println("°C");
#endif

    config.data.centralHeating = !server.arg("heat").isEmpty();
    config.data.hotWater = !server.arg("water").isEmpty();
    config.data.cooling = !server.arg("cooling").isEmpty();
    config.data.manualBoilerTemp = !server.arg("manual").isEmpty();
#ifdef NEED_SERIAL_PRINT
    Serial.print("Setup boiler flags: centralHeating=");
    Serial.print(config.data.centralHeating);
    Serial.print("; hotWater=");
    Serial.print(config.data.hotWater);
    Serial.print("; cooling=");
    Serial.print(config.data.cooling);
    Serial.print("; manualBoilerTemp=");
    Serial.print(config.data.manualBoilerTemp);
    Serial.println();
#endif

    update = true;
  }

  if (update)
  {
    config.write();

    server.sendHeader("Location", "/", true);
    server.send(302);
    return;
  }

  unsigned long secs = (millis() - lastExternalTempUpdateTime) / 1000;
  sprintf(
    htmlAnswer,
    INDEX_TEMPLATE,
    externalTemp,
    secs,
    boilerTemp,
    lastSetupTemp ? enableColor : disableColor,
    lastSetupTemp ? successText : errorText,
    actualBoilerTemp,
    lastSetupStatus ? enableColor : disableColor,
    lastSetupStatus ? successText : errorText,
    isCentralHeating ? enableColor : disableColor,
    isCentralHeating ? enableText : disableText,
    isHotWater ? enableColor : disableColor,
    isHotWater ? enableText : disableText,
    isCooling ? enableColor : disableColor,
    isCooling ? enableText : disableText,
    isFlame ? enableColor : disableColor,
    isFlame ? enableText : disableText,
    config.data.desiredTemp,
    config.data.centralHeating ? checkboxChecked : checkboxUnchecked,
    config.data.hotWater ? checkboxChecked : checkboxUnchecked,
    config.data.cooling ? checkboxChecked : checkboxUnchecked,
    config.data.manualBoilerTemp ? checkboxChecked : checkboxUnchecked,
    config.data.desiredBoilerTemp,
    config.data.ssid
  );
  server.send(200, "text/html", htmlAnswer);
}

void handleTemp()
{
  String tempStr = server.arg("temp");
  if (server.method() != HTTP_POST || tempStr.isEmpty())
  {
    server.send(400);
    return;
  }

  priorExternalTempUpdateTime = lastExternalTempUpdateTime;
  lastExternalTempUpdateTime = millis();
  priorExternalTemp = externalTemp;
  externalTemp = parseFloat(tempStr);

#ifdef NEED_SERIAL_PRINT
  Serial.print("Received external temp: ");
  Serial.print(tempStr);
  Serial.print("°C / parsed: ");
  Serial.print(externalTemp);
  Serial.println("°C");
#endif

  server.send(200);
}

float parseFloat(String value)
{
  value.replace(",", ".");
  return value.toFloat();
}

float pid(float sp, float pv, float pv_last, float& ierr, float dt)
{
  float Kc = 12.0; // K / %Heater
  float tauI = 50.0; // sec
  float tauD = 1.0;  // sec
  // PID coefficients
  float KP = Kc;
  float KI = Kc / tauI;
  float KD = Kc * tauD;
  // upper and lower bounds on heater level
  float ophi = 55;
  float oplo = 20;
  // calculate the error
  float error = sp - pv;
  // calculate the integral error
  ierr = ierr + KI * error * dt;
  // calculate the measurement derivative
  float dpv = (pv - pv_last) / dt;
  // calculate the PID output
  float P = KP * error; //proportional contribution
  float I = ierr; //integral contribution
  float D = -KD * dpv; //derivative contribution
  float op = P + I + D;
  // implement anti-reset windup
  if ((op < oplo) || (op > ophi))
  {
    I = I - KI * error * dt;
    // clip output
    op = max(oplo, min(ophi, op));
  }
  ierr = I;
#ifdef NEED_SERIAL_PRINT
  Serial.println("PID: sp=" + String(sp) + " pv=" + String(pv) + " dt=" + String(dt) + " op=" + String(op) + " P=" + String(P) + " I=" + String(I) + " D=" + String(D));
#endif
  return op;
}
