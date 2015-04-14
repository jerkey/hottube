
OneWire ds(DS18S20_Pin);  // on digital pin 2
byte DS18S20addr[8]; // device address for our temperature sensor

float celsiusToFarenheit(float celsius) {
  float farenheit = (celsius / 5.0 * 9.0 ) + 32.0;
  return farenheit;
}

float farenheitToCelsius(float farenheit) {
  float celsius = (farenheit - 32.0) / 9.0 * 5.0;
  return celsius;
}

boolean initTemp() {
  if ( !ds.search(DS18S20addr)) {
      Serial.println("DS18S20 sensor not found!");
      ds.reset_search(); //no more sensors on chain, reset search
      return false;
  }

  if ( OneWire::crc8( DS18S20addr, 7) != DS18S20addr[7]) {
      Serial.println("DS18S20 CRC is not valid!");
      return false;
  }

  if ( DS18S20addr[0] != 0x10 && DS18S20addr[0] != 0x28) {
      Serial.print("Device is not a DS18S20");
      return false;
  }
  return true;
}

float getTemp(){
  //returns the temperature from one DS18S20 in DEG Celsius

  byte DS18S20data[12]; // data used to talk to device

  ds.reset();
  ds.select(DS18S20addr);
  ds.write(0x44,1); // start conversion, with parasite power on at the end

  delay(850); // wait for the conversion or else you get old data

  byte present = ds.reset();
  ds.select(DS18S20addr);
  ds.write(0xBE); // Read Scratchpad

  
  for (int i = 0; i < 9; i++) { // we need 9 bytes
    DS18S20data[i] = ds.read();
  }
 
  float celsius = ( (DS18S20data[1] << 8) + DS18S20data[0] )*0.0625;
  if (celsius < 2) {
    celsius = 212; // assume it's too hot if sensor isn't working
    initTemp(); // try to find the sensor again
  }
  return celsius;
}
