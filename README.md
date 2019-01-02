# CRF Access Logger

## Purpose

This project was created by the sole purpose of fun. We wanted to a way to track when our members were active and came up with this card-scanning terminal.

## Overview

Usage of the terminal is made up of simple actions. Each action has a uniqe id made from the digits 0,1,2 and 3. They are all "state-less", meaning that no state need to be held by the device or the server.

Each action corresponds to a request, and the result returned by the backend is displayed on the screen for a few seconds, or dismissed earlier by any button press.

On the device, the user first enters the code for the action to be done, and then scans their card. Commands starting with "2" requires two cards to be scanned before the request is sent to the backend. This can be used to possibly give members machine access using an admin card in the future.

The following actions are currently implemented

* Register a new user and card
* Link a new card with and existing user
* Remove a card
* Set your username
* View status (checked in/out)
* Check in/out

For further detatils, see the instruction guide on the wall next to the terminal at CRF.

## Frontend

### Hardware

The terminal consists of the following hardware components

* An ESP8266 development board.
* A 4x20 white-on-blue LCD display with a [PCF8574](http://www.ti.com/lit/ds/symlink/pcf8574.pdf) I2C 8-bit I/O expander backpack.
* A simple RC522 RFID reader

### Compiling

To compile and upload the software, you first need to make sure you have all of the following the dependencies installed:

* [ESP8266 Arduino Core](https://github.com/esp8266/Arduino)
* [MFRC522](https://github.com/esp8266/Arduino) library for using the MFRC522 RFID reader
* The sha265 library of [ESP8266-Arduino-cryptolibs](https://github.com/CSSHL/ESP8266-Arduino-cryptolibs) for creating sha256 hashes
* The [NewLiquidCrystal](https://bitbucket.org/fmalpartida/new-liquidcrystal) library for talking to the backpacked LCD display

Then rename the file ``wifi_settings.example.h`` to ``wifi_settings.h`` and edit it with your own wifi credentials and backend HTTP endpoint URL.

Then select the board ``NodeMCU 1.0 (ESP-12E Module)`` and upload the sketch.

### Technical details

Each time the terminal is used, a request to the backend is sent via HTTP. The device does a simple GET request to the specified API endpoint URL with the following header fields:

* `X-Action` - the string representing the action to execute (as entered by the user)
* `X-Card` - the full sha256 card hash
* `X-Card2` - used when an action starting with '2' is requested, holds the full sha256 hash of the second card

The backend then responds with an ``HTTP 200 OK`` and the text to display on the screen, with a maximum 80 characters. If a ``200 OK`` is not recieved, the device shows the returned HTTP code and the recieved text.

## Backend [WIP]

The backend is currently written in PHP and hosted by our local server at CRF. It uses a MySQL database for storing users, cards and all the generated check in/out events.

It also hosts a webpage showing who is checked in and some statistics on the time spent at CRF.

More information together with the code will hopefully be published soon.
