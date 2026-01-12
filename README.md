# HouseTest
A suite of test tools for the House suite of applications.

## Overview

Theses are tools to help test the House suite of applications:

* housetest is an automated web client that executes each HTTP requests listed in sequence. Continuation lines are used for the client data (POST requests). The data returned is printed out.
* housesimio is a web service that implement the House's control interface, without interacting with any physical device. This is used to test services such as sprinkler or light control.

The housesimio can also serves as a repository of calculated points.

A calculated point is not a real IO point. Instead it stores a value calculated by a separate application. Otherwise a calculated point behaves like any other IO point, accessed through the same API. This is a way to trick applications that consumes IO points and allow other applications to drive them by controlling the values of these calculated points.

## Installation.

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install

## Configuration

The housesimio service saves its configuration to a local or depot file (simio.json). The syntax matches the example below:
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

## Point states

HouseSimio accepts any arbitrary string as a point state, but the following states are known and follow specific behaviors:

- off (the device is not active, considered the normal state for most points),
- on (the device is active, this is a temporary state for most points),
- alert (in this state, a set request to `clear` actually sets the point to `on`; set requests to `clear` are ignored if the point is not currently in the `alert` state).

The logic here is that an IO point is normally `off` (inactive) or `on` (active). From some points, operators should be alerted with some visual effects (like an animation) when the point transitions to active: this is when the `alert` state should be used. The operator can then stop the visual effect by "clearing" the alert.

An example of an alert is a camera detecting motion: this transitions the matching calculated point to `alert`. When the motion detection event ends, the point is transitioned back to off. In between an operator may clear the alert, which transitions the point from `alert` to `on`.

## Web API

The housesimio service implements the [House control web API](https://github.com/pascal-fb-martin/houseportal/blob/master/controlapi.md).

## Debian Packaging

The provided Makefile supports building private Debian packages. These are _not_ official packages:

- They do not follow all Debian policies.

- They are not built using Debian standard conventions and tools.

- The packaging is not separate from the upstream sources, and there is
  no source package.

To build a Debian package, use the `debian-package` target:

``` 
make debian-package
``` 

