# esp_matrix_mqtt

Time is received via MQTT, at regular intervals the board will send out to topic `iot/sendtime`
with a payload of the device name.

To send the time (i use node red watching for the payload from `iot/sendtime`) respond to `iot/DeviceName/cmd/settime` with payload of the current unix time:
example from NodeRed debug:

`{"_msgid":"c8672fbf.8c85e","payload":1601837757,"topic":"iot/espClockDisplay02/cmd/settime"}`

To display a text message send to `iot/DeviceName/cmd/message` with payload of the message to scroll on the matrix

check the `callback` function for other features, controlled via `iot/DeviceName/cmd/COMMAND`
