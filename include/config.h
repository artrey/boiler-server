#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <Arduino.h>
#include <EEPROM.h>

struct ConfigData {
  char ssid[24];
  char pass[24];
  float desiredTemp;
  bool centralHeating;
  bool hotWater;
  bool cooling;
  bool manualBoilerTemp;
  float desiredBoilerTemp;
};

class EEPROMConfig
{
private:
  int m_offset;

public:
  ConfigData data;

public:
  EEPROMConfig(int offset) : m_offset(offset) {}

  ConfigData const& read()
  {
    return EEPROM.get<ConfigData>(m_offset, data);
  }

  ConfigData const& write()
  {
    auto& result = EEPROM.put<ConfigData>(m_offset, data);
    EEPROM.commit();
    return result;
  }

  ConfigData const& write(int pos, byte value)
  {
    EEPROM.put<byte>(m_offset + pos, value);
    EEPROM.commit();
    return read();
  }
};

#endif
