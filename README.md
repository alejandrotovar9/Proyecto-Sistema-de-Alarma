# Alarm System with Telegram Bot using ESP32
Alarm System using ESP32 and Telegram. Sends a notificaction using a telegram bot depending on wether certain areas are configured as critical or non-critical. 
Non-critical areas emit a buzzer sound if someone passes in front of a PIR sensor connected to one of the pins and critical areas, 
if a pin that acts as a fingerprint sensor is not pressed, send a Telegram message using a bot to the user or to a group, depending on the TOKEN used.

Things to add:
-Token
-SSID
-WiFi Password

Things to be added to the project:
-UI to configure sensors.
-Dedicated App to use instead of Telegram.
-Protocols of saving new configurations in case system gets disconnected.
-Security method in case the RF+PIR sensor gets stolen or disconnected.


