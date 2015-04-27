<!-- Generate a C header file from the Modem Manager specification.

Copyright (C) 2006, 2007 Collabora Limited
Copyright (C) 2011 Google, Inc.

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
-->
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:tp="http://telepathy.freedesktop.org/wiki/DbusSpec#extensions-v0"
  exclude-result-prefixes="tp">

  <!--Don't move the declaration of the HTML namespace up here - XMLNSs
  don't work ideally in the presence of two things that want to use the
  absence of a prefix, sadly. -->

  <xsl:strip-space elements="node interface"/>
  <xsl:template match="*" mode="identity">
    <xsl:copy>
      <xsl:apply-templates mode="identity"/>
    </xsl:copy>
  </xsl:template>

  <xsl:template match="signal">
    <xsl:variable name="varname">
      <xsl:value-of select="translate(@name,
			    'abcdefghijklmnopqrstuvwxyz. ',
			    'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
    </xsl:variable>
    <xsl:variable name="intname">
      <xsl:choose>
        <xsl:when test="starts-with(../@name, 'org.freedesktop.ModemManager1.')">
          <xsl:value-of select="translate(substring-after(../@name, 'org.freedesktop.ModemManager1.'),
			       'abcdefghijklmnopqrstuvwxyz. ',
			       'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
         </xsl:when>
         <xsl:otherwise>
           <xsl:text>MANAGER</xsl:text>
         </xsl:otherwise>
       </xsl:choose>
    </xsl:variable>
#define <xsl:value-of select="concat('MM_', $intname, '_SIGNAL_', $varname)"/> "<xsl:value-of select="@name"/>"</xsl:template>

  <xsl:template match="property">
    <xsl:variable name="varname">
      <xsl:value-of select="translate(@name,
			    'abcdefghijklmnopqrstuvwxyz. ',
			    'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
    </xsl:variable>
    <xsl:variable name="intname">
      <xsl:choose>
        <xsl:when test="starts-with(../@name, 'org.freedesktop.ModemManager1.')">
          <xsl:value-of select="translate(substring-after(../@name, 'org.freedesktop.ModemManager1.'),
			       'abcdefghijklmnopqrstuvwxyz. ',
			       'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
         </xsl:when>
         <xsl:otherwise>
           <xsl:text>MANAGER</xsl:text>
         </xsl:otherwise>
       </xsl:choose>
    </xsl:variable>
#define <xsl:value-of select="concat('MM_', $intname, '_PROPERTY_', $varname)"/> "<xsl:value-of select="@name"/>"</xsl:template>

  <xsl:template match="method">
    <xsl:variable name="varname">
      <xsl:value-of select="translate(@name,
			    'abcdefghijklmnopqrstuvwxyz. ',
                            'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
    </xsl:variable>
    <xsl:variable name="intname">
      <xsl:choose>
        <xsl:when test="starts-with(../@name, 'org.freedesktop.ModemManager1.')">
          <xsl:value-of select="translate(substring-after(../@name, 'org.freedesktop.ModemManager1.'),
			       'abcdefghijklmnopqrstuvwxyz. ',
			       'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
         </xsl:when>
         <xsl:otherwise>
           <xsl:text>MANAGER</xsl:text>
         </xsl:otherwise>
       </xsl:choose>
    </xsl:variable>
#define <xsl:value-of select="concat('MM_', $intname, '_METHOD_', $varname)"/> "<xsl:value-of select="@name"/>"</xsl:template>

  <xsl:output method="text" indent="no" encoding="ascii"
    omit-xml-declaration="yes" />

  <xsl:template match="/tp:spec">
/* Generated Header file do not edit */

/*
 * ModemManager Interface Specification
 * Version 0.6
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright (C) 2008 - 2009 Novell, Inc.
 * Copyright (C) 2009 - 2012 Red Hat, Inc.
 * Copyright (C) 2011 - 2012 Google, Inc.
 * Copyright (C) 2015 - Riccardo Vangelisti riccardo.vangelisti@sadel.it
 * Copyright (C) 2015 - Marco Bascetta marco.bascetta@sadel.it
 */

#ifndef _MODEM_MANAGER_NAMES_H_
#define _MODEM_MANAGER_NAMES_H_

#define MM_DBUS_PATH    "/org/freedesktop/ModemManager1"
#define MM_DBUS_SERVICE "org.freedesktop.ModemManager1"

/* Prefix for object paths */
#define MM_DBUS_MODEM_PREFIX  MM_DBUS_PATH "/Modem"
#define MM_DBUS_BEARER_PREFIX MM_DBUS_PATH "/Bearer"
#define MM_DBUS_SIM_PREFIX    MM_DBUS_PATH "/SIM"
#define MM_DBUS_SMS_PREFIX    MM_DBUS_PATH "/SMS"
#define MM_DBUS_CALL_PREFIX   MM_DBUS_PATH "/Call"

/* Prefix for DBus errors */
#define MM_DBUS_ERROR_PREFIX "org.freedesktop.ModemManager1.Error"

/**************
 * Interfaces *
 **************/
<xsl:for-each select="node/interface">
  <xsl:apply-templates select="tp:docstring"/>
  <xsl:variable name="varname">
    <xsl:choose>
      <xsl:when test="starts-with(@name, 'org.freedesktop.ModemManager1.')">
        <xsl:value-of select="translate(substring-after(@name, 'org.freedesktop.ModemManager1.'),
                              'abcdefghijklmnopqrstuvwxyz. ',
                              'ABCDEFGHIJKLMNOPQRSTUVWXYZ_')"/>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text></xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:variable name="prefix">
    <xsl:choose>
      <xsl:when test="string-length($varname) > 0">
        <xsl:text>_</xsl:text>
      </xsl:when>
      <xsl:otherwise>
        <xsl:text></xsl:text>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:variable>
  <xsl:if test="starts-with(@name, 'org.freedesktop.ModemManager1')">
#define <xsl:value-of select="concat('MM_DBUS_INTERFACE', $prefix, $varname)"/> "<xsl:value-of select="@name"/>"</xsl:if></xsl:for-each>

/******************************
 * Methods/Signals/Properties *
 ******************************/
<xsl:for-each select="node/interface">
/*
 * Interface '<xsl:value-of select="@name"/>'
 */
 <xsl:apply-templates select="method"/>
 <xsl:if test="count(method[*])!=0">
   <xsl:text></xsl:text>
 </xsl:if>
 <xsl:apply-templates select="signal"/>
 <xsl:if test="count(signal[*])!=0">
   <xsl:text></xsl:text>
 </xsl:if>
 <xsl:apply-templates select="property"/>
 <xsl:if test="count(property[*])!=0">
   <xsl:text></xsl:text>
 </xsl:if>
</xsl:for-each>

#endif /*  _MODEM_MANAGER_NAMES_H_ */

<!-- Ensure that the file ends with a newline -->
<xsl:text>
</xsl:text>
</xsl:template>
</xsl:stylesheet>

<!-- vim:set sw=2 sts=2 et: -->
