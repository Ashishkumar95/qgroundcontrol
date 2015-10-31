/*=====================================================================
 
 QGroundControl Open Source Ground Control Station
 
 (c) 2009 - 2014 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 
 This file is part of the QGROUNDCONTROL project
 
 QGROUNDCONTROL is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 QGROUNDCONTROL is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with QGROUNDCONTROL. If not, see <http://www.gnu.org/licenses/>.
 
 ======================================================================*/

/// @file
///     @author Don Gagne <don@thegagnes.com>

#include "APMParameterMetaData.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

QGC_LOGGING_CATEGORY(APMParameterMetaDataLog, "APMParameterMetaDataLog")

bool APMParameterMetaData::_parameterMetaDataLoaded = false;
QPointer<Vehicle> APMParameterMetaData::_vehicle    = NULL;
QMap<QString, FactMetaData*> APMParameterMetaData::_mapParameterName2FactMetaData;

APMParameterMetaData::APMParameterMetaData(QObject* parent) :
    QObject(parent)
{
    _loadParameterFactMetaData();
//    _vehicle = vehicle;
}

/// Converts a string to a typed QVariant
///     @param string String to convert
///     @param type Type for Fact which dictates the QVariant type as well
///     @param convertOk Returned: true: conversion success, false: conversion failure
/// @return Returns the correctly type QVariant
QVariant APMParameterMetaData::_stringToTypedVariant(const QString& string, FactMetaData::ValueType_t type, bool* convertOk)
{
    QVariant var(string);

    int convertTo = QVariant::Int; // keep compiler warning happy
    switch (type) {
        case FactMetaData::valueTypeUint8:
        case FactMetaData::valueTypeUint16:
        case FactMetaData::valueTypeUint32:
            convertTo = QVariant::UInt;
            break;
        case FactMetaData::valueTypeInt8:
        case FactMetaData::valueTypeInt16:
        case FactMetaData::valueTypeInt32:
            convertTo = QVariant::Int;
            break;
        case FactMetaData::valueTypeFloat:
            convertTo = QMetaType::Float;
            break;
        case FactMetaData::valueTypeDouble:
            convertTo = QVariant::Double;
            break;
    }
    
    *convertOk = var.convert(convertTo);
    
    return var;
}

QString APMParameterMetaData::mavTypeToString(MAV_TYPE vehicleTypeEnum)
{
    QString vehicleName;

    switch(vehicleTypeEnum) {
        case MAV_TYPE_FIXED_WING:
            vehicleName = "ArduPlane";
            break;
        case MAV_TYPE_QUADROTOR:
        case MAV_TYPE_COAXIAL:
        case MAV_TYPE_HELICOPTER:
        case MAV_TYPE_SUBMARINE:
        case MAV_TYPE_HEXAROTOR:
        case MAV_TYPE_OCTOROTOR:
        case MAV_TYPE_TRICOPTER:
            vehicleName = "ArduCopter";
            break;
        case MAV_TYPE_ANTENNA_TRACKER:
            vehicleName = "Antenna Tracker";
        case MAV_TYPE_GENERIC:
        case MAV_TYPE_GCS:
        case MAV_TYPE_AIRSHIP:
        case MAV_TYPE_FREE_BALLOON:
        case MAV_TYPE_ROCKET:
            break;
        case MAV_TYPE_GROUND_ROVER:
        case MAV_TYPE_SURFACE_BOAT:
            vehicleName = "ArduRover";
            break;
        case MAV_TYPE_FLAPPING_WING:
        case MAV_TYPE_KITE:
        case MAV_TYPE_ONBOARD_CONTROLLER:
        case MAV_TYPE_VTOL_DUOROTOR:
        case MAV_TYPE_VTOL_QUADROTOR:
        case MAV_TYPE_VTOL_TILTROTOR:
        case MAV_TYPE_VTOL_RESERVED2:
        case MAV_TYPE_VTOL_RESERVED3:
        case MAV_TYPE_VTOL_RESERVED4:
        case MAV_TYPE_VTOL_RESERVED5:
        case MAV_TYPE_GIMBAL:
        case MAV_TYPE_ENUM_END:
        default:
            break;
    }
    return vehicleName;
}

/// Load Parameter Fact meta data
///
/// The meta data comes from firmware parameters.xml file.
void APMParameterMetaData::_loadParameterFactMetaData(void)
{
    if (_parameterMetaDataLoaded) {
        return;
    }
    _parameterMetaDataLoaded = true;

    QString vehicleName = mavTypeToString(MAV_TYPE_QUADROTOR);

    qCDebug(APMParameterMetaDataLog) << "Loading APM parameter fact meta data for vehicle type" << vehicleName;

    Q_ASSERT(_mapParameterName2FactMetaData.count() == 0);

    QString parameterFilename;

    // We want unit test builds to always use the resource based meta data to provide repeatable results
    if (parameterFilename.isEmpty() || !QFile(parameterFilename).exists()) {
        parameterFilename = ":/FirmwarePlugin/APM/apm.pdef.xml";
    }

    qCDebug(APMParameterMetaDataLog) << "Loading parameter meta data:" << parameterFilename;

    QFile xmlFile(parameterFilename);
    Q_ASSERT(xmlFile.exists());

    bool success = xmlFile.open(QIODevice::ReadOnly);
    Q_UNUSED(success);
    Q_ASSERT(success);

    QXmlStreamReader xml(xmlFile.readAll());
    xmlFile.close();
    if (xml.hasError()) {
        qCWarning(APMParameterMetaDataLog) << "Badly formed XML, reading failed: " << xml.errorString();
        return;
    }

    QString           errorString;
    FactMetaData*     metaData = NULL;
    int               xmlState = XmlStateNone;
    bool              badMetaData = true;
    QMap<QString,QStringList> groupMembers; //used to remove groups with single item

    while (!xml.atEnd()) {
        if (xml.isStartElement()) {
            QString elementName = xml.name().toString();

            // keep skipping anything that is not parameters start
            if (elementName != "parameters" && xmlState == XmlStateNone) {
                xml.readNext();
                continue;
            }

            if (elementName == "parameters") {
                if (xmlState != XmlStateNone) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, element parameters ";
                    return;
                }

                if (xml.attributes().hasAttribute("name")) {
                    // we will handle metadata only for the interested MAV_TYPE and libraries
                    if (xml.attributes().value("name") == vehicleName ||
                            xml.attributes().value("name") == "libraries") {
                        if (xmlState != XmlStateNone) {
                            qCWarning(APMParameterMetaDataLog) << "Badly formed XML element vehicle or libraries";
                            return;
                        }
                        xmlState = XmlStateFoundParameters;
                    } else {
                        qCDebug(APMParameterMetaDataLog) << "not interested in this block of parameters skip";
                        if (skipParameterXMLBlock(xml, "parameters")) {
                            qDebug() << "something wrong with the xml";
                            return;
                        }
                        xml.readNext();
                        continue;
                    }
                }
            }  else if (elementName == "param") {
                if (xmlState != XmlStateFoundParameters) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, element param";
                    return;
                }
                xmlState = XmlStateFoundParameter;

                if (!xml.attributes().hasAttribute("name")) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, parameter name";
                    return;
                }

                QString name = xml.attributes().value("name").toString();
                if (name.contains(':')) {
                    name = name.split(':').last();
                }
                QString group = name.split('_').first();
                group = group.remove(QRegExp("[0-9]*$")); // remove any nubmers from the end

                QString shortDescription = xml.attributes().value("humanName").toString();
                QString longDescription = xml.attributes().value("docmentation").toString();
                QString userLevel = xml.attributes().value("user").toString();

                qCDebug(APMParameterMetaDataLog) << "Found parameter name:" << name
                          << "short Desc:" << shortDescription
                          << "longDescription:" << longDescription
                          << "user level: " << userLevel
                          << "group: " << group;

                metaData = new FactMetaData();
                Q_CHECK_PTR(metaData);
                if (_mapParameterName2FactMetaData.contains(name)) {
                    // We can't trust the meta dafa since we have dups
                    qCWarning(APMParameterMetaDataLog) << "Duplicate parameter found:" << name;
                    badMetaData = true;
                    // Reset to default meta data
                    _mapParameterName2FactMetaData[name] = metaData;
                } else {
                    qCDebug(APMParameterMetaDataLog) << "inserting metadata for field" << name;
                    _mapParameterName2FactMetaData[name] = metaData;
                    metaData->setName(name);
                    metaData->setGroup(group);
                    metaData->setShortDescription(shortDescription);
                    metaData->setLongDescription(longDescription);

                    groupMembers[group] << name;

                    qCDebug(APMParameterMetaDataLog) << "APM parameter description doesn't specify a default value";
                }

            } else {
                // We should be getting meta data now
                if (xmlState != XmlStateFoundParameter) {
                    qCWarning(APMParameterMetaDataLog) << "Badly formed XML, while reading parameter fields wrong state";
                    return;
                }
                if (!badMetaData) {
                    if (!parseParameterAttributes(xml, metaData)) {
                        qCDebug(APMParameterMetaDataLog) << "Badly formed XML, failed to read parameter attributes";
                        return;
                    }
                    continue;
                }
            }
        } else if (xml.isEndElement()) {
            QString elementName = xml.name().toString();

            if (elementName == "param" && xmlState == XmlStateFoundParameter) {
                // Done loading this parameter, validate default value
                if (metaData->defaultValueAvailable()) {
                    QVariant var;

                    if (!metaData->convertAndValidate(metaData->defaultValue(), false /* convertOnly */, var, errorString)) {
                        qCWarning(APMParameterMetaDataLog) << "Invalid default value, name:" << metaData->name() << " type:" << metaData->type() << " default:" << metaData->defaultValue() << " error:" << errorString;
                    }
                }

                // Reset for next parameter
                metaData = NULL;
                badMetaData = false;
                xmlState = XmlStateFoundParameters;
            } else if (elementName == "parameters") {
                qCDebug(APMParameterMetaDataLog) << "end of parameters";
                xmlState = XmlStateNone;
            }
        }
        xml.readNext();
    }

    foreach(const QString& groupName, groupMembers.keys()) {
        if (groupMembers[groupName].count() == 1) {
            foreach(const QString& parameter, groupMembers.value(groupName)) {
                _mapParameterName2FactMetaData[parameter]->setGroup("others");
            }
        }
    }
}

bool APMParameterMetaData::skipParameterXMLBlock(QXmlStreamReader &xml, const QString &blockName)
{
    QString elementName;
    do {
        xml.readNext();
        elementName = xml.name().toString();
    } while ((elementName != blockName) && (xml.isEndElement()));
    return !xml.isEndDocument();
}

bool APMParameterMetaData::parseParameterAttributes(QXmlStreamReader &xml, FactMetaData *metaData)
{
    Q_ASSERT(metaData);
    QString elementName = xml.name().toString();
    while (elementName == "field") {
        QString attributeName = xml.attributes().value("name").toString();
        if ( attributeName == "range") {
            QString range = xml.readElementText();
            QStringList rangeList = range.split(' ');
            if (rangeList.count() != 2) {
                qCWarning(APMParameterMetaDataLog) << "something wrong with range, ignoring error";
            }
            QString min = rangeList.first();
            QString max = rangeList.last();
            metaData->setMin(min);
            metaData->setMax(max);
            qCDebug(APMParameterMetaDataLog) << "read field parameter " << "min: " << min << "max: " << max;
        } else if (attributeName == "Increment") {
            QString increment = xml.readElementText();
            qCDebug(APMParameterMetaDataLog) << "read Increment: " << increment;
            metaData->setShortDescription(metaData->shortDescription() + "increment size: " + increment);
        } else if (attributeName == "Units") {
            QString units = xml.readElementText();
            qCDebug(APMParameterMetaDataLog) << "read Units: " << units;
            metaData->setUnits(units);
        }
        xml.readNext();
        elementName = xml.name().toString();
    }

    if (elementName == "values") {
        Q_ASSERT(metaData);
        xml.readNext();

        QString elementName = xml.name().toString();
        if (elementName != "value") {
            qCDebug(APMParameterMetaDataLog) << "Badly fomrmed xml, failed to read parameter";
            return false;
        }

        QList<QPair<QString,QString> > values;
        while (elementName == "value") {
            QString valueValue = xml.attributes().value("code").toString();
            QString valueName = xml.readElementText();
            qCDebug(APMParameterMetaDataLog) << "read value parameter " << "value desc: " << valueName << "code: " << valueValue;
            values << QPair<QString,QString> (valueValue, valueName);
            xml.readNext();
            elementName = xml.name().toString();
        }
    } else {
        qCDebug(APMParameterMetaDataLog) << "Unknown parameter element in XML: " << elementName;
    }
    return true;
}

/// Override from FactLoad which connects the meta data to the fact
void APMParameterMetaData::addMetaDataToFact(Fact* fact)
{
    _loadParameterFactMetaData();


    FactMetaData* metaData = new FactMetaData(fact->type(), fact);
    if (_mapParameterName2FactMetaData.contains(fact->name())) {
        fact->setMetaData(_mapParameterName2FactMetaData[fact->name()]);
    } else {
        // Use generic meta data
        ParameterMetaData::addMetaDataToFact(fact);
    }
}