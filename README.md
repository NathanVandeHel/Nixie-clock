# Nixie-clock



Arduino based project for a custom Nixie clock made with four IN-8 tubes.

The clock has the followings functionalities :

- Time can be edited easily
- The hours can be displayed in 12h or 24h format
- The leading zero on the hours can be hidden 
- Eco mode allows to turn off the tubes between an editable time period
- Bypass possibility of the eco mode for one period by pressing a button
- RTC allows for keeping track of time when the clock is unpowered
- Settings also saved on the RTC for when the clock in unpowered



## Components

The clock uses the following components :

- a [XIAO SAMD21](https://wiki.seeedstudio.com/Seeeduino-XIAO/) for control
- a small form factor 5-9V to 170V DC [power supply](https://fr.aliexpress.com/item/4000001969913.html?spm=a2g0o.order_list.0.0.17045e5bG34hkj&gatewayAdapt=glo2fra) for the tubes
- a [DS3231 RTC](https://www.amazon.fr/gp/product/B07WJSQ6M2/ref=ppx_yo_dt_b_asin_title_o01_s00?ie=UTF8&psc=1) module for an accurate time reading (and to keep time when the clock is not powered)
- four F155ID1 74141 tube drivers (74141 version allows for no output state, which is needed for the "hide leading zero" function)
- two SN74HC595N shift registers in series to manage the tube drivers
- two buttons to manage the different functions



## Dependencies

The project depends on two libraries to communicate with the RTC module :

- Wire
- The [ds3231](https://github.com/rodan/ds3231) library by Petre Rodan (manual installation needed)



## PCB

PCB created on EasyEDA v6.5.1.



## Improvements

- The RTC module battery cannot be replaced easily for now

