# HouseTest
A suite of test tools for the House suite of applications.

## Overview

Theses are tools to help test the House suite of applications:

* housetest is an automated web client that executes each HTTP requests listed in sequence. Continuation lines are used for the client data (POST requests). The data returned is printed out.
* housesimio is a web service that implement the House's control interface, without interacting with any physical device. This is used to test services such as sprinkler or light control.

## Installation.

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install
* start housesimio.

## Configuration

The housesimio service saves its configuration to a local file (simio.json). The syntax matches the example below:
```
{
    "simio" : {
        "points" : [
            {
                "name" : "point1",
                "gear" : "valve"
            },
            {
                "name" : "point2",
                "gear" : "valve"
            }
        ]
    }
}
```

## Web API

The housesimio service implements the House control web API. See [HouseRelays](https://github.com/pascal-fb-martin/houserelays) for more details.

