//####################### Vincent Huynen #######################/
//################## vincent.huynen@gmail.com ##################/
//######################### AUGUST 2022 ######################/
//#################### CubeCell HELTEC AB01 ####################/
//######################## Version 1.0.0 #######################/

/*
  LoRaWan Rain Gauge
  The tipping bucket rain gauge has a magnetic reed switch that closes momentarily each time the gauge measures 0.011" (0.2794 mm) of rain.
*/

#include "LoRaWan_APP.h"
#include "Arduino.h"
#include <CayenneLPP.h>

/* OTAA para*/
uint8_t devEui[] = {};
uint8_t appEui[] = {};
uint8_t appKey[] = {};

/* ABP para*/
uint8_t nwkSKey[] = {};
uint8_t appSKey[] = {};
uint32_t devAddr = (uint32_t)0x00;

/*LoraWan channelsmask, default channels 0-7*/
uint16_t userChannelsMask[6] = {0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};

// The interrupt pin is attached to GPIO1
#define RAIN_GAUGE_PIN GPIO1

bool wakeUp = false;
int rainGaugeCounter = 0;
int cycleCounter = 0;
int batteryVoltage;
bool ENABLE_SERIAL = true; // Enable serial debug output here if required

/*LoraWan region, select in arduino IDE tools*/
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

/*LoraWan Class, Class A and Class C are supported*/
DeviceClass_t loraWanClass = LORAWAN_CLASS;

/*the application data transmission duty cycle.  value in [ms].*/
/*For this example, this is the frequency of the device status packets */
uint32_t appTxDutyCycle = 900000; // Default 15 mins
uint32_t watchDogTimer = 86400000; // Daily health check

/*OTAA or ABP*/
bool overTheAirActivation = LORAWAN_NETMODE;

/*ADR enable*/
bool loraWanAdr = LORAWAN_ADR;

/* set LORAWAN_Net_Reserve ON, the node could save the network info to flash, when node reset not need to join again */
bool keepNet = LORAWAN_NET_RESERVE;

/* Indicates if the node is sending confirmed or unconfirmed messages */
bool isTxConfirmed = LORAWAN_UPLINKMODE;

/* Application port */
uint8_t appPort = 1;

/*!
 * Number of trials to transmit the frame, if the LoRaMAC layer did not
 * receive an acknowledgment.
 */
uint8_t confirmedNbTrials = 4;

/* Prepares the payload of the frame */
static void prepareTxFrame()
{
  float vbat = getBatteryVoltage();

  CayenneLPP lpp(8);
  lpp.reset();
  lpp.addDigitalInput(15, rainGaugeCounter);
  lpp.addAnalogInput(8, vbat / 1000);
  appDataSize = lpp.getSize();
  lpp.copy(appData);

  if (ENABLE_SERIAL)
  {
    Serial.println();
    Serial.println("Rain gauge counter: " + String(rainGaugeCounter));
    Serial.println("Vbat in mV: " + String(vbat));
    Serial.println("Cycle Counter: " + String(cycleCounter));
    Serial.println("Time: " + String(cycleCounter * appTxDutyCycle));
  }
  rainGaugeCounter = 0;
}

void rainGaugeWakeUp()
{
  detachInterrupt(RAIN_GAUGE_PIN);
  rainGaugeCounter++;
  wakeUp = true;
  //!\\ Debounce reed switch
  delay(500);
}

void setup()
{

  if (ENABLE_SERIAL)
  {
    Serial.begin(115200);
  }

  deviceState = DEVICE_STATE_INIT;
  LoRaWAN.ifskipjoin();

  wakeUp = false;
  pinMode(RAIN_GAUGE_PIN, INPUT_PULLUP);
  attachInterrupt(RAIN_GAUGE_PIN, rainGaugeWakeUp, FALLING);
}

void loop()
{
  if (wakeUp)
  {
    if (ENABLE_SERIAL)
    {
      Serial.println("\nIt's Raining Men !");
    }
  }

  switch (deviceState)
  {
  case DEVICE_STATE_INIT:
  {
    printDevParam();
    LoRaWAN.init(loraWanClass, loraWanRegion);
    deviceState = DEVICE_STATE_JOIN;
    break;
  }
  case DEVICE_STATE_JOIN:
  {
    LoRaWAN.join();
    break;
  }
  case DEVICE_STATE_SEND:
  {
    cycleCounter++;
    if (rainGaugeCounter > 0 || (cycleCounter * appTxDutyCycle) > watchDogTimer)
    {
      prepareTxFrame();
      if (IsLoRaMacNetworkJoined)
      {
        LoRaWAN.send();
      }
      cycleCounter = 0;
    }
    deviceState = DEVICE_STATE_CYCLE;
    break;
  }
  case DEVICE_STATE_CYCLE:
  {
    // Schedule next packet transmission
    txDutyCycleTime = appTxDutyCycle + randr(0, APP_TX_DUTYCYCLE_RND);
    LoRaWAN.cycle(txDutyCycleTime);
    deviceState = DEVICE_STATE_SLEEP;
    break;
  }
  case DEVICE_STATE_SLEEP:
  {
    if (wakeUp)
    {
      attachInterrupt(RAIN_GAUGE_PIN, rainGaugeWakeUp, FALLING);
      wakeUp = false;
    }
    LoRaWAN.sleep();
    break;
  }
  default:
  {
    deviceState = DEVICE_STATE_INIT;
    break;
  }
  }
}

// downlink data handling function (from https://github.com/jthiller/)
void downLinkDataHandle(McpsIndication_t *mcpsIndication)
{
  Serial.printf("+REV DATA:%s,RXSIZE %d,PORT %d\r\n", mcpsIndication->RxSlot ? "RXWIN2" : "RXWIN1", mcpsIndication->BufferSize, mcpsIndication->Port);

  switch (mcpsIndication->Port)
  {
  case 1:
    /* Set update interval on port 1 */
    if (ENABLE_SERIAL)
      {
        Serial.println("Setting new update interval.");
        Serial.print("Hours: ");
        Serial.println(mcpsIndication->Buffer[0]);
        Serial.print("Minutes: ");
        Serial.println(mcpsIndication->Buffer[1]);
      }
    // Multiply slot 1 by 1 hr, slot 2 by 1 minute.
    // Values (0-255) submitted in hex.
    // Use a tool like https://v2.cryptii.com/decimal/base64.
    // e.g. `0 10` -> `AAo=` Representing 10 minute interval.
    appTxDutyCycle = (3600000 * mcpsIndication->Buffer[0]) + (60000 * mcpsIndication->Buffer[1]);
    break;
  case 2:
    // For other purposes
    break;
  default:
    break;
  }
}