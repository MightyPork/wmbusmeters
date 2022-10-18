/*
 Copyright (C) 2021-2022 Fredrik Öhrström (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include"meters_common_implementation.h"

/*
 * This decoder is for version 0x46 of QHeat 5.5
 *
 * Q OMS / Q AMR / Q WBC / Q WBS
 * Q heat 5.5 (US)R
 */

namespace
{
    struct Driver : public virtual MeterCommonImplementation
    {
        Driver(MeterInfo &mi, DriverInfo &di);
    };

    static bool ok = registerDriver([](DriverInfo&di)
    {
        di.setName("qheat_55_usr");
        di.setMeterType(MeterType::HeatMeter);
        di.addLinkMode(LinkMode::C1);

        // 41449344796550674637727965506793444604dc0000200c0d000000004c0d00000000426cffffcc080d00000000c2086cdf2802fd170000326cffff046d3a0ddb29
        di.addDetection(MANUFACTURER_QDS, 0x04,  0x46);

        // 3C (60) is probably the walk-by version, no sample yet
        //di.addDetection(MANUFACTURER_QDS, 0x04,  0x3C);

        di.setConstructor([](MeterInfo& mi, DriverInfo& di){ return shared_ptr<Meter>(new Driver(mi, di)); });
    });

    Driver::Driver(MeterInfo &mi, DriverInfo &di) :  MeterCommonImplementation(mi, di)
    {
        // 023   : 0C dif (8 digit BCD Instantaneous value)
        // 024   : 0D vif (Energy 10⁵ J)
        addNumericFieldWithExtractor(
            "total_energy_consumption",
            "The total energy consumption recorded by this meter.",
            PrintProperty::FIELD | PrintProperty::JSON | PrintProperty::IMPORTANT,
            Quantity::Energy,
            VifScaling::Auto,
            FieldMatcher::build()
            .set(DifVifKey("0C0D"))
            // FIXME - don't use DifVifKey - this does not work:
            //  .set(MeasurementType::Instantaneous)
            //  .set(VIFRange::AnyEnergyVIF)
            , Unit::GJ
            );

        // 029   : 4C dif (8 digit BCD Instantaneous value storagenr=1)
        // 030   : 0D vif (Energy 10⁵ J)
        addNumericFieldWithExtractor(
            "cutoff_date_energy_consumption",
            "The total energy consumption recorded at cut-off date.",
            PrintProperty::FIELD | PrintProperty::JSON | PrintProperty::IMPORTANT,
            Quantity::Energy,
            VifScaling::Auto,
            FieldMatcher::build()
            .set(DifVifKey("4C0D"))
            // FIXME - don't use DifVifKey - this does not work:
            //  .set(MeasurementType::Instantaneous)
            //  .set(VIFRange::AnyEnergyVIF)
            //  .set(StorageNr(1)),
            , Unit::GJ
            );

        // value is FFFF ("2127-15-31") before the cutoff date occurs for the first time
        addStringFieldWithExtractor(
                "cutoff_date",
                "Cut-off date",
                PrintProperty::JSON,
                FieldMatcher::build()
                        .set(MeasurementType::Instantaneous)
                        .set(StorageNr(1))
                        .set(VIFRange::Date)
        );

        addStringFieldWithExtractor(
            "last_month_date",
             "Last day previous month when total energy consumption was recorded.",
            PrintProperty::FIELD | PrintProperty::JSON | PrintProperty::IMPORTANT,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(StorageNr(17))
            .set(VIFRange::Date)
            );

        // 039   : CC dif (8 digit BCD Instantaneous value storagenr=1)
        // 040   : 08 dife (subunit=0 tariff=0 storagenr=17)
        // 041   : 0D vif (Energy 10⁵ J)
        addNumericFieldWithExtractor(
            "last_month_energy_consumption",
            "The total energy consumption recorded at the last day of the previous month.",
            PrintProperty::FIELD | PrintProperty::JSON,
            Quantity::Energy,
            VifScaling::Auto,
            FieldMatcher::build()
            .set(DifVifKey("CC080D"))
            // FIXME - don't use DifVifKey - this does not work:
            //  .set(MeasurementType::Instantaneous)
            //  .set(StorageNr(17))
            //  .set(VIFRange::AnyEnergyVIF)
            , Unit::GJ
            );

        addStringFieldWithExtractor(
            "device_date_time",
             "Device date time.",
            PrintProperty::JSON,
            FieldMatcher::build()
            .set(MeasurementType::Instantaneous)
            .set(VIFRange::DateTime)
            );

        addStringFieldWithExtractor(
            "device_error_date",
             "Device error date.",
            PrintProperty::JSON,
            FieldMatcher::build()
            .set(MeasurementType::AtError)
            .set(VIFRange::Date)
            );

        addStringFieldWithExtractorAndLookup(
            "error_flags",
            "Error flags.",
            PrintProperty::JSON | PrintProperty::FIELD | JOIN_TPL_STATUS,
            FieldMatcher::build()
            .set(VIFRange::ErrorFlags),
            {
                {
                    {
                        "ERROR_FLAGS",
                        Translate::Type::BitToString,
                        0xffff,
                        "OK",
                        {
                            // !!! Uncertain !!!
                            // The documentation links to some intranet site, Qundis Error Codes Specification v1.7
                            // at https://base/svn/sys/Meter/Gen/G5-5/PUBL/Gen55_SysSpec_Error-Codes_EN_v1.6_any.pdf

                            // Here is a table that might apply:
                            // https://www.manualslib.com/manual/2046543/Qundis-Q-Heat-5-5-Us.html?page=5

                            { 0x01, "NO_FLOW" }, // F0
                            { 0x02, "SUPPLY_SENSOR_INTERRUPTED" }, // F1
                            { 0x04, "RETURN_SENSOR_INTERRUPTED" }, // F2
                            { 0x08, "TEMPERATURE_ELECTRONICS_ERROR" }, // F3
                            { 0x10, "BATTERY_VOLTAGE_ERROR" }, // F4
                            { 0x20, "SHORT_CIRCUIT_SUPPLY_SENSOR" }, // F5
                            { 0x40, "SHORT_CIRCUIT_RETURN_SENSOR" }, // F6
                            { 0x80, "MEMORY_ERROR" }, // F7
                            { 0x100, "SABOTAGE" }, // F8 - F1,2,3,5,6 longer than 8 hours, latching error, no more measurements performed.
                            { 0x200, "ELECTRONICS_ERROR" }, // F9
                        }
                    },
                },
            });
    }
}

// TODO update this:
// New telegram: 41449344796550674637727965506793444604dc0000200c0d000000004c0d00000000426cffffcc080d00000000c2086cdf2802fd170000326cffff046d3a0ddb29

// Test: QHeato qheat 67228058 NOKEY
// telegram=|3C449344957002372337725880226793442304DC0000200C05043900004C0500000000426C9F2CCC080551070000C2086CBE29326CFFFF046D280DB62A|
// {"media":"heat","meter":"qheat","name":"QHeato","id":"67228058","total_energy_consumption_kwh":390.4,"last_month_date":"2021-09-30","last_month_energy_consumption_kwh":75.1,"last_year_date":"2020-12-31","last_year_energy_consumption_kwh":0,"device_date_time":"2021-10-22 13:40","device_error_date":"2127-15-31","timestamp":"1111-11-11T11:11:11Z"}
// |QHeato;67228058;390.400000;2021-09-30;75.100000;1111-11-11 11:11.11
