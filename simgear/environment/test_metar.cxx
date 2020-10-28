
#ifdef HAVE_CONFIG_H
#  include <simgear_config.h>
#endif

#include <simgear/compiler.h>
#include <simgear/misc/test_macros.hxx>

#include <iostream>
#include <cstdlib>
#include <cstdio>

#ifdef _MSC_VER
#   define  random  rand
#endif

#include <simgear/misc/sg_dir.hxx>
#include <simgear/structure/exception.hxx>

#include "metar.hxx"

using std::cout;
using std::cerr;
using std::endl;
using std::string;


const double TEST_EPSILON = 1e-9;

void test_basic()
{
    SGMetar m1("2011/10/20 11:25 EHAM 201125Z 27012KT 240V300 9999 VCSH FEW025CB SCT048 10/05 Q1025 TEMPO VRB03KT");
    SG_CHECK_EQUAL(m1.getYear(), 2011);
    SG_CHECK_EQUAL(m1.getMonth(), 10);
    SG_CHECK_EQUAL(m1.getDay(), 20);
    SG_CHECK_EQUAL(m1.getHour(), 11);
    SG_CHECK_EQUAL(m1.getMinute(), 25);
    SG_CHECK_EQUAL(m1.getReportType(), -1); // should default to NIL?

    SG_CHECK_EQUAL(m1.getWindDir(), 270);
    SG_CHECK_EQUAL_EP2(m1.getWindSpeed_kt(), 12, TEST_EPSILON);

    SG_CHECK_EQUAL(m1.getWeather().size(), 1);
    SG_CHECK_EQUAL(m1.getClouds().size(), 2);

    SG_CHECK_EQUAL_EP2(m1.getTemperature_C(), 10, TEST_EPSILON);
    SG_CHECK_EQUAL_EP2(m1.getDewpoint_C(), 5, TEST_EPSILON);
    SG_CHECK_EQUAL_EP2(m1.getPressure_hPa(), 1025, TEST_EPSILON);
}

void test_sensor_failure_weather()
{
    SGMetar m1("2011/10/20 11:25 EHAM 201125Z 27012KT 240V300 9999 // FEW025CB SCT048 10/05 Q1025");
    SG_CHECK_EQUAL(m1.getWindDir(), 270);
    SG_CHECK_EQUAL_EP2(m1.getWindSpeed_kt(), 12, TEST_EPSILON);

    SG_CHECK_EQUAL(m1.getWeather().size(), 0);
    SG_CHECK_EQUAL(m1.getClouds().size(), 2);

    SG_CHECK_EQUAL_EP2(m1.getTemperature_C(), 10, TEST_EPSILON);
    SG_CHECK_EQUAL_EP2(m1.getDewpoint_C(), 5, TEST_EPSILON);
    SG_CHECK_EQUAL_EP2(m1.getPressure_hPa(), 1025, TEST_EPSILON);
}

void test_sensor_failure_cloud()
{
    SGMetar m1("2011/10/20 11:25 EHAM 201125Z 27012KT 240V300 9999 FEW025CB/// SCT048/// 10/05 Q1025");
    SG_CHECK_EQUAL(m1.getWindDir(), 270);
    SG_CHECK_EQUAL_EP2(m1.getWindSpeed_kt(), 12, TEST_EPSILON);

    SG_CHECK_EQUAL(m1.getWeather().size(), 0);
    SG_CHECK_EQUAL(m1.getClouds().size(), 2);

    SG_CHECK_EQUAL_EP2(m1.getTemperature_C(), 10, TEST_EPSILON);
    SG_CHECK_EQUAL_EP2(m1.getDewpoint_C(), 5, TEST_EPSILON);
    SG_CHECK_EQUAL_EP2(m1.getPressure_hPa(), 1025, TEST_EPSILON);
}

void test_sensor_failure_wind()
{
    SGMetar m1("2020/10/23 16:55 LIVD 231655Z /////KT 9999 OVC025 10/08 Q1020 RMK OVC VIS MIN 9999 BLU");
    SG_CHECK_EQUAL(m1.getWindDir(), -1);
    SG_CHECK_EQUAL_EP2(m1.getWindSpeed_kt(), -1, TEST_EPSILON);
}

void test_wind_unit_not_specified()
{
    SGMetar m1("2020/10/23 11:58 KLSV 231158Z 05010G14 10SM CLR 16/M04 A2992 RMK SLPNO WND DATA ESTMD ALSTG/SLP ESTMD 10320 20124 5//// $");
    SG_CHECK_EQUAL(m1.getWindDir(), 50);
    SG_CHECK_EQUAL_EP2(m1.getWindSpeed_kt(), 10.0, TEST_EPSILON);
    SG_CHECK_EQUAL_EP2(m1.getGustSpeed_kt(), 14.0, TEST_EPSILON);
}

int main(int argc, char* argv[])
{
    try {
        test_basic();
        test_sensor_failure_weather();
        test_sensor_failure_cloud();
        test_sensor_failure_wind();
        test_wind_unit_not_specified();
    } catch (sg_exception& e) {
        cerr << "got exception:" << e.getMessage() << endl;
        return -1;
    }
    
    return 0;
}
